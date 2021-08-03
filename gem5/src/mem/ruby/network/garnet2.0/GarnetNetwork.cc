/*
 * Copyright (c) 2008 Princeton University
 * Copyright (c) 2016 Georgia Institute of Technology
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met: redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer;
 * redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution;
 * neither the name of the copyright holders nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Authors: Niket Agarwal
 *          Tushar Krishna
 */


#include "mem/ruby/network/garnet2.0/GarnetNetwork.hh"

#include <cassert>

#include "base/cast.hh"
#include "base/stl_helpers.hh"
#include "debug/RubyNetwork.hh"
#include "mem/ruby/common/NetDest.hh"
#include "mem/ruby/system/Sequencer.hh"
#include "mem/ruby/network/MessageBuffer.hh"
#include "mem/ruby/network/garnet2.0/CommonTypes.hh"
#include "mem/ruby/network/garnet2.0/CreditLink.hh"
#include "mem/ruby/network/garnet2.0/GarnetLink.hh"
#include "mem/ruby/network/garnet2.0/NetworkInterface.hh"
#include "mem/ruby/network/garnet2.0/NetworkLink.hh"
#include "mem/ruby/network/garnet2.0/Router.hh"
#include "mem/ruby/network/garnet2.0/RoutingUnit.hh"
#include "mem/ruby/network/garnet2.0/InputUnit.hh"
#include "mem/ruby/network/garnet2.0/OutputUnit.hh"
#include "mem/ruby/system/RubySystem.hh"

#include "mem/ruby/network/garnet2.0/flit.hh"
//end

using namespace std;
using m5::stl_helpers::deletePointers;

/*
 * GarnetNetwork sets up the routers and links and collects stats.
 * Default parameters (GarnetNetwork.py) can be overwritten from command line
 * (see configs/network/Network.py)
 */

GarnetNetwork::GarnetNetwork(const Params *p)
    : Network(p)
{
    m_num_rows = p->num_rows;
    m_ni_flit_size = p->ni_flit_size;
    m_vcs_per_vnet = p->vcs_per_vnet;
    m_buffers_per_data_vc = p->buffers_per_data_vc;
    m_buffers_per_ctrl_vc = p->buffers_per_ctrl_vc;
    m_routing_algorithm = p->routing_algorithm;

    max_flit_latency = Cycles(0);
    max_flit_network_latency = Cycles(0);
    max_flit_queueing_latency = Cycles(0);

    min_flit_latency = Cycles(999999); // 1 million
    min_flit_network_latency = Cycles(999999);
    min_flit_queueing_latency = Cycles(999999);

    print_trigger = Cycles(100); // init with 100
	// interswap-defaults
	m_interswap = p->interswap;
	m_policy = p->policy;
    m_whenToSwap = p->whenToSwap;
    m_whichToSwap = p->whichToSwap;
    m_no_is_swap = p->no_is_swap;
    m_occupancy_swap = p->occupancy_swap;
    m_inj_single_vnet = p->inj_single_vnet;

    cout << "m_inj_single_vnet: " << m_inj_single_vnet << endl;
    if (m_inj_single_vnet == 0) {
        // when injecting in multiple VNets,
        // do not get into the complexity of 'is_swap' bit
        m_no_is_swap = 1;
    }
    if (m_interswap) {
        assert(m_policy == 0); // to make sure we don't use this at all
        // If interswap is set then 'whenToSwap' and 'whichToSwap' should
        // not be equal to 0. Assert.
        assert(m_whenToSwap != 0);
        assert(m_whichToSwap != 0);
        #if (MY_PRINT)
            cout << "***********************************" << endl;
            cout << "interSwap is enabled" << endl;
            cout << "***********************************" << endl;
        #endif

        if (m_whenToSwap == TDM_) {
            #if (MY_PRINT)
                cout << "***********************************" << endl;
                cout << " 'whenToSwap' :::: TDM Swap Policy is used" << endl;
                cout << "***********************************" << endl;
            #endif
        } else if (m_whenToSwap == _2_TDM_) {
            #if (MY_PRINT)
                cout << "***********************************" << endl;
                cout << " 'whenToSwap' :::: " << endl;
                cout << "Swaps will be initiated by the upstream router on "\
                        "twice its TDM turn. Therefore it has half the "\
                        "frequency of swaps compared to defualt policy" << endl;
                cout << "***********************************" << endl;
            #endif
        } else if (m_whenToSwap == _4_TDM_) {
            #if (MY_PRINT)
                cout << "***********************************" << endl;
                cout << " 'whenToSwap' :::: " << endl;
                cout << "Swaps will be initiated by the upstream router on "\
                        "twice its TDM turn. Therefore it has half the "\
                        "frequency of swaps compared to defualt policy" << endl;
                cout << "***********************************" << endl;
            #endif
        } else if (m_whenToSwap == _8_TDM_) {
            #if (MY_PRINT)
                cout << "***********************************" << endl;
                cout << " 'whenToSwap' :::: " << endl;
                cout << "Swaps will be initiated by the upstream router on "\
                        "twice its TDM turn. Therefore it has half the "\
                        "frequency of swaps compared to defualt policy" << endl;
                cout << "***********************************" << endl;
            #endif
        } else if (m_whenToSwap == _16_TDM_) {
            #if (MY_PRINT)
                cout << "***********************************" << endl;
                cout << " 'whenToSwap' :::: " << endl;
                cout << "Swaps will be initiated by the upstream router "\
                        "on its 16th TDM turn. Therefore it has 1/16th "\
                        "of the frequency of swaps compared to default policy" << endl;
                cout << "***********************************" << endl;
            #endif
        } else if (m_whenToSwap == _32_TDM_) {
            #if (MY_PRINT)
                cout << "***********************************" << endl;
                cout << " 'whenToSwap' :::: " << endl;
                cout << "Swaps will be initiated by the upstream router "\
                        "on its 32th TDM turn. Therefore it has 1/32th "\
                        "of the frequency of swaps compared to default policy" << endl;
                cout << "***********************************" << endl;
            #endif
        } else {
            #if (MY_PRINT)
                cout << "***********************************" << endl;
                cout << "Unimplermented 'whenToSwap' knob has been selected."\
                        " Aborting simulation..." << endl;
                cout << "***********************************" << endl;
            #endif
            // assert(0);
        }

        if (m_whichToSwap == DISABLE_LOCAL_SWAP_) {
            #if(MY_PRINT)
                cout << "***********************************" << endl;
                cout << " 'whichToSwap' :::: Local ports at upstream router "\
                        "will not take part in the swap initiated" << endl;
                cout << "***********************************" << endl;
            #endif
        } else if (m_whichToSwap == ENABLE_LOCAL_SWAP_) {
            #if(MY_PRINT)
                cout << "***********************************" << endl;
                cout << " 'whenToSwap' :::: Local ports at upstream router "\
                        "will take part in the swap initiated" << endl;
                cout << "***********************************" << endl;
            #endif
        } else {
            #if (MY_PRINT)
                cout << "***********************************" << endl;
                cout << "Unimplermented 'whichToSwap' knob has been selected. "\
                        "Aborting simulation..." << endl;
                cout << "***********************************" << endl;
            #endif
                assert(0);
       }
    } else {
        #if (MY_PRINT)
            cout << "***********************************" << endl;
            cout << "interSwap is disabled" << endl;
            cout << "***********************************" << endl;
        #endif
        assert(m_whenToSwap == 0);
        assert(m_whichToSwap == 0);
        assert(m_policy == 0);
    }

    m_enable_fault_model = p->enable_fault_model;
    if (m_enable_fault_model)
        fault_model = p->fault_model;

    m_vnet_type.resize(m_virtual_networks);

    for (int i = 0 ; i < m_virtual_networks ; i++) {
        if (m_vnet_type_names[i] == "response")
            m_vnet_type[i] = DATA_VNET_; // carries data (and ctrl) packets
        else
            m_vnet_type[i] = CTRL_VNET_; // carries only ctrl packets
    }

    // record the routers
    for (vector<BasicRouter*>::const_iterator i =  p->routers.begin();
         i != p->routers.end(); ++i) {
        Router* router = safe_cast<Router*>(*i);
        m_routers.push_back(router);

        // initialize the router's network pointers
        router->init_net_ptr(this);
    }

    // record the network interfaces
    for (vector<ClockedObject*>::const_iterator i = p->netifs.begin();
         i != p->netifs.end(); ++i) {
        NetworkInterface *ni = safe_cast<NetworkInterface *>(*i);
        m_nis.push_back(ni);
        ni->init_net_ptr(this);
    }
}

void
GarnetNetwork::init()
{
    #if (MY_PRINT)
    	cout << "GarnetNetwork::init() gets called" << endl;
    #endif
    Network::init();

    for (int i=0; i < m_nodes; i++) {
        m_nis[i]->addNode(m_toNetQueues[i], m_fromNetQueues[i]);
    }

    // The topology pointer should have already been initialized in the
    // parent network constructor
    assert(m_topology_ptr != NULL);
    m_topology_ptr->createLinks(this);

    // Initialize topology specific parameters
    if (getNumRows() > 0) {
        // Only for Mesh topology
        // m_num_rows and m_num_cols are only used for
        // implementing XY or custom routing in RoutingUnit.cc
        m_num_rows = getNumRows();
        m_num_cols = m_routers.size() / m_num_rows;
        assert(m_num_rows * m_num_cols == m_routers.size());
    } else {
        m_num_rows = -1;
        m_num_cols = -1;
    }

    // FaultModel: declare each router to the fault model
    if (isFaultModelEnabled()) {
        for (vector<Router*>::const_iterator i= m_routers.begin();
             i != m_routers.end(); ++i) {
            Router* router = safe_cast<Router*>(*i);
            int router_id M5_VAR_USED =
                fault_model->declare_router(router->get_num_inports(),
                                            router->get_num_outports(),
                                            router->get_vc_per_vnet(),
                                            getBuffersPerDataVC(),
                                            getBuffersPerCtrlVC());
            assert(router_id == router->get_id());
            router->printAggregateFaultProbability(cout);
            router->printFaultVector(cout);
        }
    }
	Sequencer::gnet = this;
    // for deadlock detection; if want to do periodically
    last_probe = 0;
}

GarnetNetwork::~GarnetNetwork()
{
    deletePointers(m_routers);
    deletePointers(m_nis);
    deletePointers(m_networklinks);
    deletePointers(m_creditlinks);
}

/*
 * This function creates a link from the Network Interface (NI)
 * into the Network.
 * It creates a Network Link from the NI to a Router and a Credit Link from
 * the Router to the NI
*/

void
GarnetNetwork::makeExtInLink(NodeID src, SwitchID dest, BasicLink* link,
                            const NetDest& routing_table_entry)
{
    assert(src < m_nodes);

    GarnetExtLink* garnet_link = safe_cast<GarnetExtLink*>(link);

    // GarnetExtLink is bi-directional
    NetworkLink* net_link = garnet_link->m_network_links[LinkDirection_In];
    net_link->setType(EXT_IN_);
    CreditLink* credit_link = garnet_link->m_credit_links[LinkDirection_In];

    m_networklinks.push_back(net_link);
    m_creditlinks.push_back(credit_link);

    PortDirection dst_inport_dirn = "Local";
    m_routers[dest]->addInPort(dst_inport_dirn, net_link, credit_link);
    m_nis[src]->addOutPort(net_link, credit_link, dest);
}

/*
 * This function creates a link from the Network to a NI.
 * It creates a Network Link from a Router to the NI and
 * a Credit Link from NI to the Router
*/

void
GarnetNetwork::makeExtOutLink(SwitchID src, NodeID dest, BasicLink* link,
                             const NetDest& routing_table_entry)
{
    assert(dest < m_nodes);
    assert(src < m_routers.size());
    assert(m_routers[src] != NULL);

    GarnetExtLink* garnet_link = safe_cast<GarnetExtLink*>(link);

    // GarnetExtLink is bi-directional
    NetworkLink* net_link = garnet_link->m_network_links[LinkDirection_Out];
    net_link->setType(EXT_OUT_);
    CreditLink* credit_link = garnet_link->m_credit_links[LinkDirection_Out];

    m_networklinks.push_back(net_link);
    m_creditlinks.push_back(credit_link);

    PortDirection src_outport_dirn = "Local";
    m_routers[src]->addOutPort(src_outport_dirn, net_link,
                               routing_table_entry,
                               link->m_weight, credit_link);
    m_nis[dest]->addInPort(net_link, credit_link);
}

/*
 * This function creates an internal network link between two routers.
 * It adds both the network link and an opposite credit link.
*/

void
GarnetNetwork::makeInternalLink(SwitchID src, SwitchID dest, BasicLink* link,
                                const NetDest& routing_table_entry,
                                PortDirection src_outport_dirn,
                                PortDirection dst_inport_dirn)
{
    GarnetIntLink* garnet_link = safe_cast<GarnetIntLink*>(link);

    // GarnetIntLink is unidirectional
    NetworkLink* net_link = garnet_link->m_network_link;
    net_link->setType(INT_);
    CreditLink* credit_link = garnet_link->m_credit_link;

    m_networklinks.push_back(net_link);
    m_creditlinks.push_back(credit_link);

    m_routers[dest]->addInPort(dst_inport_dirn, net_link, credit_link);
    m_routers[src]->addOutPort(src_outport_dirn, net_link,
                               routing_table_entry,
                               link->m_weight, credit_link);
}

// Total routers in the network
int
GarnetNetwork::getNumRouters()
{
    return m_routers.size();
}

// Get ID of router connected to a NI.
int
GarnetNetwork::get_router_id(int ni)
{
    return m_nis[ni]->get_router_id();
}

int
GarnetNetwork::get_downstreamId( PortDirection outport_dir, int upstream_id )
{
    int num_cols = getNumCols();
    int downstream_id = -1; // router_id for downstream router
    /*outport direction fromt he flit for this router*/
    if (outport_dir == "East") {
        downstream_id = upstream_id + 1;
    }
    else if (outport_dir == "West") {
        downstream_id = upstream_id - 1;
    }
    else if (outport_dir == "North") {
        downstream_id = upstream_id + num_cols;
    }
    else if (outport_dir == "South") {
        downstream_id = upstream_id - num_cols;
    }
    else if (outport_dir == "Local"){
        #if (MY_PRINT)
            cout << "outport_dir: " << outport_dir << endl;
        #endif
        assert(0);
        return -1;
    }
    else {
        #if (MY_PRINT)
            cout << "outport_dir: " << outport_dir << endl;
        #endif
        assert(0); // for completion of if-else chain
        return -1;
    }

    return downstream_id;
}

Router*
GarnetNetwork::get_downstreamRouter(PortDirection outport_dir, int upstream_id)
{
    int router_id = -1;
    router_id = get_downstreamId( outport_dir, upstream_id);
//    cout << "downstream router-id: " << router_id << endl;
    // assert(router_id >= 0);
    if ((router_id < 0) || (router_id >= getNumRouters()))
        return NULL;
    else
        return m_routers[router_id];
}


PortDirection
GarnetNetwork::get_downstreamDirn( PortDirection outport_dir )
{
    // 'inport_dirn' of the downstream router
    // NOTE: it's Mesh specific
    PortDirection inport_dirn;
    if (outport_dir == "East") {
        inport_dirn = "West";
    } else if (outport_dir == "West") {
        inport_dirn = "East";
    } else if (outport_dir == "North") {
        inport_dirn = "South";
    } else if (outport_dir == "South") {
        inport_dirn = "North";
    } else if (outport_dir == "Local") {
        assert(0); // shouldn't come here,,,
    }

    return inport_dirn;
}


// if this fucntion returns 'true' initiate bail_out sequence.
// NOte: it still do bail-put signalling based on invc 0 for ech
// inport.
// That is, if outport of flit at invc0 has all downstream router's
// `is_swap` bit set then it will trigger the bailout sequence.
// Implecation: More false positives.. not incorrect.
// Reason: Can introduce many bugs; more code addition wo incentive
bool
GarnetNetwork::chk_deadlck_symptm(int my_id, int vnet)
{



    if(curCycle()%1000 == 0)
        return true;
     else
        return false;
    // -----------------------------

    // movSwapPtr in the end.. first peekTopFlit() of upstream
    // after checking vc_isEmpty; then check the correspnding
    // `is_swap` bit of downstream.
    Router* router = m_routers[my_id];
    // loop over its inport starting from swap_ptr.inport
    uint32_t orig_inport = router->swap_ptr.inport;
    uint32_t inport_itr = router->swap_ptr.inport;
    // Convention: Empty: -1; dontBailOut: 7; bailOut: 1;
    // std::vector<int> bail_out; // indexed by 'inport'
    int num_inports = router->get_num_inports();
    // nothing to worry.. this is just a local variable of this func
    std::vector<std::vector<int>> bail_out(num_inports);
    //bail_out.resize(router->get_num_inports());
    int downstreamId;
    PortDirection downstreamInportDirn;
    int downstreamInportId;

    int vcs_per_vnet = router->get_vc_per_vnet();
    // int vnet = 0;
    int vc_base = vnet*vcs_per_vnet;

    for (int k = 0; k < num_inports; ++k) {
        bail_out[k].resize(vcs_per_vnet);
    }

    bool last_itr = false; // flag which when set, would trigger the
                           // completion of while loop in next iteration.

    while (1) { // loop for populating 'bail_out' for upstream router
        if (last_itr == true)
            break; // only exit point from loop

        inport_itr++;
        if (inport_itr == router->get_num_inports())
            inport_itr = 0; // looping_over

        if (inport_itr == orig_inport)  // loop completes
            last_itr = true;


        // ------fill bail_out matrix-------
        // 1. check if vc_isEmpty() is not then peekTopFlit(0)
        flit* flit_ = NULL; // upstream (my_id)'s flit
        int upstreamVcId = router->swap_ptr.vcid;
        assert(upstreamVcId != -1); // vcid shouldn't be -1
        // loop over all the invc for this given inport of upstream router
        for(int in_vc = vc_base; in_vc < vc_base + vcs_per_vnet; ++in_vc) {

            if (router->get_inputUnit_ref()[inport_itr]->vc_isEmpty(in_vc) == false)
                flit_ = router->get_inputUnit_ref()[inport_itr]->peekTopFlit(in_vc);
            else
                bail_out[inport_itr][in_vc] = -1; // mark the given entry in vec as empty

            if (flit_ != NULL) {
                // cout << *flit_ << endl;
                // we are successfully able to peek the flit of upstream router
                // assert(flit_->get_vnet() == 0);
                PortDirection outport_dir = flit_->get_outport_dir();
                if(outport_dir == "Local") // Exceptional case
                    return false; // we don't need to bail-out as this flit is going to be ejected
                downstreamId = get_downstreamId(outport_dir, my_id);
                downstreamInportDirn = get_downstreamDirn(outport_dir);
                assert(downstreamInportDirn != "Local");
                downstreamInportId = m_routers[downstreamId]->get_routingUnit_ref()\
                                            ->m_inports_dirn2idx[downstreamInportDirn];
                int vc;
                for(vc = vc_base; vc < vc_base + m_vcs_per_vnet; vc++) {
                    if ((m_routers[downstreamId]->is_swap == true) &&
                        (m_routers[downstreamId]->get_inputUnit_ref()[downstreamInportId]\
                                                ->vc_isEmpty(vc) == false))
                        continue;
                    else
                        break; // either `is_swap` is false or vc is empty (don't bailOut)
                }
                if (vc == (vc_base + m_vcs_per_vnet))
                    bail_out[inport_itr][in_vc] = 1; // need to bail_out
                else
                    bail_out[inport_itr][in_vc] = 7; // don't bail_out

            } else {
                assert(bail_out[inport_itr][in_vc] == -1); // empty inport at upstream rout
            }
        }
    }
    #if (MY_PRINT)
        cout << "bail_out Matrix for router: " << my_id << endl;
        for (int inport=0; inport < num_inports; ++inport) {
            cout << endl;
            for (int in_vc = 0; in_vc < vcs_per_vnet; ++in_vc) {
                cout << bail_out[inport][in_vc] << "\t";
            }
        }

        cout << endl;
    #endif

    // Sanity check: no entry should be 0
    for (int inport=0; inport < num_inports; ++inport)
        for (int in_vc = 0; in_vc < vcs_per_vnet; ++in_vc)
            assert(bail_out[inport][in_vc] != 0);

    //---------<bail-out matrix analysis>----------
    // bail_out Matrix must be filled completely with entry != 0
    // 1. do the analysis here on the vector
    // if no need to bail_out:
    // update the swap_ptr(inport+dirn) so that swap can happen; ret false
    for (int inport=0; inport < num_inports; ++inport) {
        for (int in_vc = 0; in_vc < vcs_per_vnet; ++in_vc) {
            assert(bail_out[inport][in_vc] != 0);
            if (bail_out[inport][in_vc] == 7) {

                if ((router->get_inputUnit_ref()[inport]->vc_isEmpty(in_vc) == false) &&
                    (router->getInportDirection(inport) != "Local")) {
                    router->swap_ptr.inport = inport;
                    router->swap_ptr.inport_dirn = router\
                    ->get_inputUnit_ref()[router->swap_ptr.inport]->get_direction();
                    router->swap_ptr.vcid = in_vc; // because we are making sure vcid0 is not empty
                }
                return false; // this will return wo completing the loop
           }
        }
    }
    if (router->is_swap == true)
        return true;
    else
        return false;
}

void
GarnetNetwork::bail_out(int my_id)
{
    // if this sequence is being called then current swap_ptr direction and [inport_id-vc_id]
    // should not be empty and the pointed outport of downstream routed should also
    // not be empty (all vcs for downstream-router). and should have `is_swap` bit set.
    // scanNetwork();
    Router* router = m_routers[my_id];
    int upstreamInport = router->swap_ptr.inport;
    int upstreamVcId = router->swap_ptr.vcid;
    int vcs_per_vnet = router->get_vc_per_vnet();
    // assert(upstreamVcId == 0);
    assert((upstreamInport != -1) && (upstreamVcId != -1));
    assert(router->get_inputUnit_ref()[upstreamInport]->vc_isEmpty(upstreamVcId) == false);
    flit* flit_ = router->get_inputUnit_ref()[upstreamInport]->peekTopFlit(upstreamVcId);
    PortDirection outport_dir = flit_->get_outport_dir();
    int downstream_id = get_downstreamId(outport_dir, my_id);
    Router* dnstream_router = m_routers[downstream_id];
    assert(vcs_per_vnet == dnstream_router->get_vc_per_vnet());
    // inport direction of downstream router
    PortDirection inport_dirn = get_downstreamDirn(outport_dir);
    assert(inport_dirn != "Local");
    int downstream_inport_id =
        dnstream_router->get_routingUnit_ref()->m_inports_dirn2idx[inport_dirn];
    // assert(dnstream_router->is_swap == true);
    // assert(router->is_swap == true);

    for (int in_vc = 0; in_vc < vcs_per_vnet; ++in_vc)
        assert(dnstream_router->get_inputUnit_ref()[downstream_inport_id]\
            ->vc_isEmpty(in_vc) == false);

    // Bail-out sequence
    // 1. clear the 'is_swap' bit of both upstream and downstream routers
    // 2. clear the 'routedSwap' bit from the flits of both upstream and downstream router
    m_routers[my_id]->is_swap = false;
    m_routers[downstream_id]->is_swap = false;
    // clear the routedSwap from upstream router.
    // int vcs_per_vnet = router->get_vc_per_vnet();
    int vnet = 0;
    int vc_base = vnet*vcs_per_vnet;
    for (int inport = 0; inport < router->get_num_inports(); inport++) {
        for (int vc = vc_base; vc < vc_base + m_vcs_per_vnet; vc++) {
            if(router->get_inputUnit_ref()[inport]->vc_isEmpty(vc))
                continue;
            else {
                if(router->get_inputUnit_ref()[inport]->peekTopFlit(vc)->get_RoutedSwap()
                    == true)
                router->get_inputUnit_ref()[inport]->peekTopFlit(vc)->unset_RoutedSwap();
            }
        }
    }
    // clear the routedSwap from dnstream router.
    for (int inport = 0; inport < dnstream_router->get_num_inports(); inport++) {
        for (int vc = vc_base; vc < vc_base + m_vcs_per_vnet; vc++) {
            if(dnstream_router->get_inputUnit_ref()[inport]->vc_isEmpty(vc))
                continue; //. shouldn't be empty though...
            else {
                if(dnstream_router->get_inputUnit_ref()[inport]->peekTopFlit(vc)->get_RoutedSwap()
                    == true)
                dnstream_router->get_inputUnit_ref()[inport]->peekTopFlit(vc)->unset_RoutedSwap();
            }
        }
    }
}


// interSwap function
// called by upstream router fo make guarantteed forward
// progress of the flit.
flit*
GarnetNetwork::doSwap(flit *flit_t, int upstream_id)
{
    #if (MY_PRINT)
        cout << "GarnetNetwork::doSwap()" << endl;
    #endif

	int downstream_id = -1;
    PortDirection inport_dirn = "Unknown";
	/*outport direction from the flit for this router*/
	PortDirection outport_dir = flit_t->get_outport_dir();
    #if (MY_PRINT)
        cout << "Head-flit's outport_dir: " <<  outport_dir << endl;
        cout <<  "Head-flit's outport: " << flit_t->get_outport() << endl;
    #endif

    downstream_id = get_downstreamId(outport_dir, upstream_id);
    // inport dirn at (wrt) downstream router
    inport_dirn = get_downstreamDirn(outport_dir);
	// Only do swap when is_swap bit is low
	// after doing the swap set it high at
	// downstream router...
	// Also the swap_ptr of downstream router
	// should be valid
	if( m_no_is_swap == 0 ) {
        if ((m_routers[downstream_id]->is_swap == false) &&
            (m_routers[downstream_id]->swap_ptr.valid == true)) {
            // get the flit from downstream router..
            // this 'inport_dirn' is of downstream router
            // we do swap with *SAME* vcid of the downstream router as indicated by
            // the swap_ptr of upstream router...
            int vcid = m_routers[upstream_id]->swap_ptr.vcid;
            assert(vcid == flit_t->get_vc());
            flit* flit1_t = m_routers[downstream_id]->doSwap(inport_dirn, vcid);
            if (flit1_t == NULL) {
                // this means that queue at downstream router is empty OR
                // mis-routed flit has Local outport
                // do not do the swap
                return NULL;
            } else {
                #if (MY_PRINT)
                    cout << "GarnetNetwork::doSwap Received a flit from downstream router" << endl;
                    cout <<  *flit1_t << endl;
                #endif
                // set 'is_swap' bit here.. this is done to
                // avoid the routed flit (which has made forward)
                // progress to get mis-routeed.
                m_routers[downstream_id]->is_swap = true;
                // set the routed swapbit here..
                // Enqueue flit_t to downstream router's inport...
                #if (MY_PRINT)
                    cout << "GarnetNetwork::doSwap enqueuing flit into downstream router" << endl;
                #endif
                flit_t->set_RoutedSwap();
                #if (MY_PRINT)
                    cout << *flit_t << endl;
                    cout << "GarnetNetwork::doSwap upstream_id: " << upstream_id << endl;
                    cout << "GarnetNetwork::doSwap downstream_id: " << downstream_id << endl;
                #endif
                m_routers[downstream_id]->doSwap_enqueue(flit_t, inport_dirn, -1, vcid);
                return flit1_t;
            }
        }
        else {

            return NULL;
        }
	}
    else if ( m_no_is_swap == 1 ) {
        if (m_routers[downstream_id]->swap_ptr.valid == true) {

            int vcid = m_routers[upstream_id]->swap_ptr.vcid;
            assert(vcid == flit_t->get_vc());
            flit* flit1_t = m_routers[downstream_id]->\
                            doSwap(inport_dirn, vcid);
            if (flit1_t == NULL) {
                #if (MY_PRINT)
                cout << "SWAP failed because downstream router did not return SWAP-back flit" << endl;
                #endif
                // this means that queue at downstream router is empty OR
                // mis-routed flit has Local outport
                // do not do the swap
                return NULL;
            } else {
                #if (MY_PRINT)
                    cout << "GarnetNetwork::doSwap Received " \
                            "a flit from downstream router" << endl;
                    cout <<  *flit1_t << endl;
                #endif

                #if (MY_PRINT)
                    cout << "GarnetNetwork::doSwap enqueuing flit "\
                            "into downstream router" << endl;
                #endif
                flit_t->set_RoutedSwap();
                #if (MY_PRINT)
                    cout << *flit_t << endl;
                    cout << "GarnetNetwork::doSwap upstream_id: " \
                        << upstream_id << endl;
                    cout << "GarnetNetwork::doSwap downstream_id: " \
                            << downstream_id << endl;
                #endif
                m_routers[downstream_id]->\
                            doSwap_enqueue(flit_t, inport_dirn, -1, vcid);
                return flit1_t;
            }
        }
        else {
            #if (MY_PRINT)
            cout << "SWAP failed because deownstream router's swap_ptr is invalid" << endl;
            #endif
            // the 'is_swap' bit at downstream router is already set..
            // OR,
            // swap_ptr at downstream router is not valid
            // don't do the swap; return NULL
            return NULL;
        }
	} else {
        assert(0);
	}

    assert(0);
}

// scanNetwork function to loop through all routers
// and print their states.
void
GarnetNetwork::scanNetwork()
{
    cout << "**********************************************" << endl;
    for (vector<Router*>::const_iterator itr= m_routers.begin();
         itr != m_routers.end(); ++itr) {
        Router* router = safe_cast<Router*>(*itr);
        cout << "--------" << endl;
        cout << "Router_id: " << router->get_id() << endl;;
        cout << "is_swap: " << router->is_swap << endl;
        if( router->swap_ptr.valid == true ) {
            cout << "swap_ptr.valid: " << router->swap_ptr.valid << endl;
            cout << "swap_ptr.inport: " << router->swap_ptr.inport << endl;
            cout << "swap_ptr.inport_dirn: " << router->swap_ptr.inport_dirn \
                << endl;

        }
        cout << "~~~~~~~~~~~~~~~" << endl;
        for (int inport = 0; inport < router->get_num_inports(); inport++) {
            // print here the inport ID and flit in that inport...
            cout << "inport: " << inport << " direction: " << router->get_inputUnit_ref()[inport]\
                                                                    ->get_direction() << endl;
            assert(inport == router->get_inputUnit_ref()[inport]->get_id());
            if(router->get_inputUnit_ref()[inport]->vc_isEmpty(0)) {
                cout << "inport is empty" << endl;
            } else {
                cout << "flit info in this inport:" << endl;
                cout << *(router->get_inputUnit_ref()[inport]->peekTopFlit(0)) << endl;
            }
        }
        if(router->is_swap == true) {
            assert(router->swap_ptr.valid == true);
        }
    }
    cout << "**********************************************" << endl;
    cout << "Link States:" << endl;
    for (vector<Router*>::const_iterator itr= m_routers.begin();
         itr != m_routers.end(); ++itr) {
        Router* router = safe_cast<Router*>(*itr);
        cout << "--------" << endl;
        cout << "Router_id: " << router->get_id() << " Cycle: " << curCycle() << endl;
        for (int outport = 0; outport < router->get_num_outports(); outport++) {
            // print here the outport ID and flit in that outport Link...
            PortDirection direction_ = router->get_outputUnit_ref()[outport]\
                                                                ->get_direction();
            // cout << "outport: " << outport << " direction: " << direction_ << endl;
            assert(outport == router->get_outputUnit_ref()[outport]->get_id());
            if(router->get_outputUnit_ref()[outport]->m_out_link->linkBuffer->isEmpty()) {
                cout << "outport: " << outport << " direction(link): " << direction_ \
                    << " is EMPTY" << endl;
            } else {
                cout << "outport: " << outport << " direction(link): " << direction_ \
                     << endl;
                flit *t_flit;
                t_flit = router->get_outputUnit_ref()[outport]->m_out_link\
                            ->linkBuffer->peekTopFlit();
                cout << *(t_flit) << endl;
                cout << "Message carried by this flit: " << endl;
                cout << *(t_flit->m_msg_ptr) << endl;
            }
        }
        cout << "--------" << endl;
    }
    return;
}

int
GarnetNetwork::get_upstreamId( PortDirection inport_dir, int my_id )
{
    int num_cols = getNumCols();
    int upstream_id = -1; // router_id for downstream router
    /*outport direction fromt he flit for this router*/
    if (inport_dir == "East") {
        upstream_id = my_id + 1;
    }
    else if (inport_dir == "West") {
        upstream_id = my_id - 1;
    }
    else if (inport_dir == "North") {
        upstream_id = my_id + num_cols;
    }
    else if (inport_dir == "South") {
        upstream_id = my_id - num_cols;
    }
    else if (inport_dir == "Local") {
        upstream_id = my_id;
        #if (MY_PRINT)
            cout << "inport_dir: " << inport_dir << endl;
        #endif
        // assert(0);
        // return -1;
    }
    else {
        #if (MY_PRINT)
            cout << "inport_dir: " << inport_dir << endl;
        #endif
        assert(0); // for completion of if-else chain
        return -1;
    }

    return upstream_id;
}


Router*
GarnetNetwork::get_upstreamrouter(PortDirection inport_dir, int upstream_id)
{
    int router_id = -1;
    router_id = get_upstreamId( inport_dir, upstream_id);
//    cout << "downstream router-id: " << router_id << endl;
    // assert(router_id >= 0);
    if ((router_id < 0) || (router_id >= getNumRouters()))
        return NULL;
    else
        return m_routers[router_id];
}



void
GarnetNetwork::regStats()
{
    Network::regStats();


    total_pre_swap_deadlock
        .name(name() + ".total_pre_swap_deadlock");

    total_post_swap_deadlock
        .name(name() + ".total_post_swap_deadlock");


    // Packets
    m_packets_received
        .init(m_virtual_networks)
        .name(name() + ".packets_received")
        .flags(Stats::pdf | Stats::total | Stats::nozero | Stats::oneline)
        ;

    m_flt_dist
        .init(m_routers.size())
        .name(name() + ".flit_distribution")
        .flags(Stats::pdf | Stats::total | Stats::nozero | Stats::oneline)
        ;

    m_network_latency_histogram
        .init(21)
        .name(name() + ".network_latency_histogram")
        .flags(Stats::pdf | Stats::total | Stats::nozero | Stats::oneline)
        ;

    m_flt_latency_hist
        .init(100)
        .name(name() + ".flit_latency_histogram")
        .flags(Stats::pdf | Stats::total | Stats::nozero | Stats::oneline)
        ;

    m_flt_network_latency_hist
        .init(100)
        .name(name() + ".flit_network_latency_histogram")
        .flags(Stats::pdf | Stats::total | Stats::nozero | Stats::oneline)
        ;

    m_flt_queueing_latency_hist
        .init(100)
        .name(name() + ".flit_queueing_latency_histogram")
        .flags(Stats::pdf | Stats::total | Stats::nozero | Stats::oneline)
        ;

    m_max_flit_latency
        .name(name() + ".max_flit_latency");
    m_max_flit_network_latency
        .name(name() + ".max_flit_network_latency");
    m_max_flit_queueing_latency
        .name(name() + ".max_flit_queueing_latency");

    m_min_flit_latency
        .name(name() + ".min_flit_latency");
    m_min_flit_network_latency
        .name(name() + ".min_flit_network_latency");
    m_min_flit_queueing_latency
        .name(name() + ".min_flit_queueing_latency");



    m_packets_injected
        .init(m_virtual_networks)
        .name(name() + ".packets_injected")
        .flags(Stats::pdf | Stats::total | Stats::nozero | Stats::oneline)
        ;

    m_packet_network_latency
        .init(m_virtual_networks)
        .name(name() + ".packet_network_latency")
        .flags(Stats::oneline)
        ;

    m_packet_queueing_latency
        .init(m_virtual_networks)
        .name(name() + ".packet_queueing_latency")
        .flags(Stats::oneline)
        ;

    for (int i = 0; i < m_virtual_networks; i++) {
        m_packets_received.subname(i, csprintf("vnet-%i", i));
        m_packets_injected.subname(i, csprintf("vnet-%i", i));
        m_packet_network_latency.subname(i, csprintf("vnet-%i", i));
        m_packet_queueing_latency.subname(i, csprintf("vnet-%i", i));
    }

    m_avg_packet_vnet_latency
        .name(name() + ".average_packet_vnet_latency")
        .flags(Stats::oneline);
    m_avg_packet_vnet_latency =
        m_packet_network_latency / m_packets_received;

    m_avg_packet_vqueue_latency
        .name(name() + ".average_packet_vqueue_latency")
        .flags(Stats::oneline);
    m_avg_packet_vqueue_latency =
        m_packet_queueing_latency / m_packets_received;

    m_avg_packet_network_latency
        .name(name() + ".average_packet_network_latency");
    m_avg_packet_network_latency =
        sum(m_packet_network_latency) / sum(m_packets_received);

    m_avg_packet_queueing_latency
        .name(name() + ".average_packet_queueing_latency");
    m_avg_packet_queueing_latency
        = sum(m_packet_queueing_latency) / sum(m_packets_received);

    m_avg_packet_latency
        .name(name() + ".average_packet_latency");
    m_avg_packet_latency
        = m_avg_packet_network_latency + m_avg_packet_queueing_latency;

    // Flits
    m_flits_received
        .init(m_virtual_networks)
        .name(name() + ".flits_received")
        .flags(Stats::pdf | Stats::total | Stats::nozero | Stats::oneline)
        ;

    m_flits_injected
        .init(m_virtual_networks)
        .name(name() + ".flits_injected")
        .flags(Stats::pdf | Stats::total | Stats::nozero | Stats::oneline)
        ;

    m_flit_network_latency
        .init(m_virtual_networks)
        .name(name() + ".flit_network_latency")
        .flags(Stats::oneline)
        ;

    m_flit_queueing_latency
        .init(m_virtual_networks)
        .name(name() + ".flit_queueing_latency")
        .flags(Stats::oneline)
        ;

    for (int i = 0; i < m_virtual_networks; i++) {
        m_flits_received.subname(i, csprintf("vnet-%i", i));
        m_flits_injected.subname(i, csprintf("vnet-%i", i));
        m_flit_network_latency.subname(i, csprintf("vnet-%i", i));
        m_flit_queueing_latency.subname(i, csprintf("vnet-%i", i));
    }

    m_avg_flit_vnet_latency
        .name(name() + ".average_flit_vnet_latency")
        .flags(Stats::oneline);
    m_avg_flit_vnet_latency = m_flit_network_latency / m_flits_received;

    m_avg_flit_vqueue_latency
        .name(name() + ".average_flit_vqueue_latency")
        .flags(Stats::oneline);
    m_avg_flit_vqueue_latency =
        m_flit_queueing_latency / m_flits_received;

    m_avg_flit_network_latency
        .name(name() + ".average_flit_network_latency");

    m_avg_flit_network_latency =
        sum(m_flit_network_latency) / sum(m_flits_received);

    m_avg_flit_queueing_latency
        .name(name() + ".average_flit_queueing_latency");
    m_avg_flit_queueing_latency =
        sum(m_flit_queueing_latency) / sum(m_flits_received);

    m_avg_flit_latency
        .name(name() + ".average_flit_latency");
    m_avg_flit_latency =
        m_avg_flit_network_latency + m_avg_flit_queueing_latency;


    // Hops
    m_avg_hops.name(name() + ".average_hops");
    m_avg_hops = m_total_hops / sum(m_flits_received);

    // Links
    m_total_ext_in_link_utilization
        .name(name() + ".ext_in_link_utilization");
    m_total_ext_out_link_utilization
        .name(name() + ".ext_out_link_utilization");
    m_total_int_link_utilization
        .name(name() + ".int_link_utilization");
    m_average_link_utilization
        .name(name() + ".avg_link_utilization");

    m_average_vc_load
        .init(m_virtual_networks * m_vcs_per_vnet)
        .name(name() + ".avg_vc_load")
        .flags(Stats::pdf | Stats::total | Stats::nozero | Stats::oneline)
        ;

    //interSwap related stats
    m_total_swaps
        .name(name() + ".total_swaps");
    m_total_bailout
        .name(name() + ".total_bailout");
    m_total_routedSwaps
        .name(name() + ".total_routedSwaps");
    m_total_initiated_swaps
        .name(name() + ".m_total_initiated_swaps");
    m_total_failed_swaps
        .name(name() + ".m_total_failed_swaps");
    m_total_failed_downstream_empty
        .name(name() + ".m_total_failed_downstream_empty");
    m_total_failed_upstream_empty
        .name(name() + ".m_total_failed_upstream_empty");
    m_total_failed_downstream_localOutport
        .name(name() + ".m_total_failed_downstream_localOutport");
    m_total_failed_upstream_localOuport
        .name(name() + ".m_total_failed_upstream_localOuport");
}

void
GarnetNetwork::collateStats()
{
    RubySystem *rs = params()->ruby_system;
    double time_delta = double(curCycle() - rs->getStartCycle());

    for (int i = 0; i < m_networklinks.size(); i++) {
        link_type type = m_networklinks[i]->getType();
        int activity = m_networklinks[i]->getLinkUtilization();

        if (type == EXT_IN_)
            m_total_ext_in_link_utilization += activity;
        else if (type == EXT_OUT_)
            m_total_ext_out_link_utilization += activity;
        else if (type == INT_)
            m_total_int_link_utilization += activity;

        m_average_link_utilization +=
            (double(activity) / time_delta);

        vector<unsigned int> vc_load = m_networklinks[i]->getVcLoad();
        for (int j = 0; j < vc_load.size(); j++) {
            m_average_vc_load[j] += ((double)vc_load[j] / time_delta);
        }
    }

    // Ask the routers to collate their statistics
    for (int i = 0; i < m_routers.size(); i++) {
        m_routers[i]->collateStats();
    }
}

void
GarnetNetwork::print(ostream& out) const
{
    out << "[GarnetNetwork]";
}

GarnetNetwork *
GarnetNetworkParams::create()
{
    return new GarnetNetwork(this);
}

uint32_t
GarnetNetwork::functionalWrite(Packet *pkt)
{
    uint32_t num_functional_writes = 0;

    for (unsigned int i = 0; i < m_routers.size(); i++) {
        num_functional_writes += m_routers[i]->functionalWrite(pkt);
    }

    for (unsigned int i = 0; i < m_nis.size(); ++i) {
        num_functional_writes += m_nis[i]->functionalWrite(pkt);
    }

    for (unsigned int i = 0; i < m_networklinks.size(); ++i) {
        num_functional_writes += m_networklinks[i]->functionalWrite(pkt);
    }

    return num_functional_writes;
}

