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


#include "mem/ruby/network/garnet2.0/Router.hh"

#include "base/stl_helpers.hh"
#include "debug/RubyNetwork.hh"
#include "mem/ruby/network/garnet2.0/CreditLink.hh"
#include "mem/ruby/network/garnet2.0/CrossbarSwitch.hh"
#include "mem/ruby/network/garnet2.0/GarnetNetwork.hh"
#include "mem/ruby/network/garnet2.0/InputUnit.hh"
#include "mem/ruby/network/garnet2.0/NetworkLink.hh"
#include "mem/ruby/network/garnet2.0/OutputUnit.hh"
#include "mem/ruby/network/garnet2.0/RoutingUnit.hh"
#include "mem/ruby/network/garnet2.0/SwitchAllocator.hh"

using namespace std;
using m5::stl_helpers::deletePointers;

Router::Router(const Params *p)
    : BasicRouter(p), Consumer(this)
{
        // initialize the router parameters
    m_latency = p->latency;
    m_virtual_networks = p->virt_nets;
    m_vc_per_vnet = p->vcs_per_vnet;
    m_num_vcs = m_virtual_networks * m_vc_per_vnet;
        // each router make these
    m_routing_unit = new RoutingUnit(this);
    m_sw_alloc = new SwitchAllocator(this);
    m_switch = new CrossbarSwitch(this);

    m_input_unit.clear();
    m_output_unit.clear();
    // initialize your 'swap_ptr' here
    curr_inport = -1;
    is_swap = false;
    swap_ptr.valid = false;
    swap_ptr.inport = -1;
    swap_ptr.vcid = -1;
    swap_ptr.vnet_id = -1;
    swap_ptr.inport_dirn = "Unknown";
    send_routedSwap = false;
    // print_trigger = Cycles(100);
}

Router::~Router()
{
    deletePointers(m_input_unit);
    deletePointers(m_output_unit);
    delete m_routing_unit;
    delete m_sw_alloc;
    delete m_switch;
}

void
Router::init()
{
        DPRINTF(RubyNetwork, "Router::init() gets called\n");
    BasicRouter::init();

    m_sw_alloc->init();
    m_switch->init();
}

int
Router::get_numFreeVC(PortDirection dirn_) {
    assert(dirn_ != "Local");
    int inport_id = m_routing_unit->m_inports_dirn2idx[dirn_];

    return (m_input_unit[inport_id]->get_numFreeVC(dirn_));
}


void
Router::vcStateDump()
{
    for (int inport=0; inport < m_input_unit.size(); ++inport) {
        cout << "inport: " << inport << "; direction: " <<
            m_routing_unit->m_inports_idx2dirn[inport] << endl;
        for (int vc_ = 0; vc_ < m_vc_per_vnet; ++vc_) {
            cout << "vcid: " << vc_ << "state: " <<
                m_input_unit[inport]->m_vcs[vc_]->get_state() << endl;
        }
    }
}

void
Router::wakeup()
{
    #if (MY_PRINT)
        cout << "isOrderedVnet[0]: " << get_net_ptr()->isVNetOrdered(0) << endl;
        cout << "Cycle: "<< curCycle() << endl;
        cout << "Router " << m_id <<" woke up" << endl;
        cout << "---[start of Router"<<m_id<<"::wakeup()]vc-state-dump-----" << endl;
        vcStateDump();
        cout << "--------------Router: " << m_id << "woke up----------------"\
            << endl;
    #endif


    #if (MY_PRINT)
        if ( curCycle() == get_net_ptr()->print_trigger ) {

            cout << "~~~~~~~~print_trigger~~~~~~~~: " << get_net_ptr()->print_trigger << endl;
            get_net_ptr()->print_trigger += (Cycles)100; /
            cout << "~~~~~~~~next-print_trigger~~~~~~~~: " << get_net_ptr()->print_trigger << endl;
            get_net_ptr()->scanNetwork();
        }
    #endif

    if (get_net_ptr()->isEnableInterswap()) {

        #if (MY_PRINT)
        cout << "this->is_myTurn(): " << this->is_myTurn() << endl;
        cout << "swap_ptr.valid: " << swap_ptr.valid << "; swap_ptr.inport: " <<
            swap_ptr.inport << "; swap_ptr.vcid: "  << swap_ptr.vcid << endl;
        cout << "-----------------------------------------------" << endl;
        #endif

        // Make swap_ptr valid here if possible...
        if ((swap_ptr.valid == false)) {
             int inport_itr;
             int invc = 0;
             bool swap_ptr_valid = false;
             for(inport_itr=0; inport_itr< m_input_unit.size(); ++inport_itr) {
                // Just check the VC-base of each VNet in that input unit
                // if there is a flit presrnt then make the swap_ptr valid.
                if(m_input_unit[inport_itr]->get_direction() == "Local") {
                    // if(get_net_ptr()->m_whichToSwap == DISABLE_LOCAL_SWAP_) {
                         continue;
                    // }
                }
                else {
                    for(int vnet=0; vnet < m_virtual_networks; ++vnet) {
                        invc = vnet*m_vc_per_vnet;
                        if ( m_input_unit[inport_itr]->vc_isEmpty(invc) == false ) {
                            flit* t_flit = m_input_unit[inport_itr]->peekTopFlit(invc);
                            m_input_unit[inport_itr]->makeSwapPtrValid(t_flit);
                            swap_ptr_valid = true;
                            break;
                        }
                        else {
                            continue;
                        }
                    }
                }
                if(swap_ptr_valid)
                    break;
             }
        }

        #if (MY_PRINT)
        cout << "-----------------------------------------------" << endl;
        cout << "this->is_myTurn(): " << this->is_myTurn() << endl;
        cout << "swap_ptr.valid: " << swap_ptr.valid << "; swap_ptr.inport: " <<
            swap_ptr.inport << "; swap_ptr.vcid: "  << swap_ptr.vcid << endl;
        #endif


        if ((this->is_myTurn()) && (swap_ptr.valid)) {
            #if (MY_PRINT)
                cout << "Router id: " << m_id <<" swap_ptr.valid: "\
                << swap_ptr.valid <<" swap_ptr.inport: " << swap_ptr.inport <<
                " swap_ptr.vcid: " << swap_ptr.vcid <<
                " swap_ptr.inport_dirn: " << swap_ptr.inport_dirn << endl;
            #endif
            assert(swap_ptr.inport_dirn ==
                    m_input_unit[swap_ptr.inport]->get_direction());

            assert((swap_ptr.inport != -1) && (swap_ptr.vcid != -1));
            // if either
            // the outport in the flit is "Local"
            // OR,
            // when there is no flit in the input queue
            // then do not do the swap; just change
            // the direction of swap_ptr of router.
            // and 'return'
            if (outportNotLocal()) {
                #if (MY_PRINT)
                    cout << "initiating the swap" << endl;
                #endif

                // Swap is initiated: (update the stats)
                get_net_ptr()->m_total_initiated_swaps++;

                // Do all of it when 'is_swap' bit is enabled
                if (get_net_ptr()->m_no_is_swap == 0) {
                    /*Check for deadlck_symtm first.*/
                    bool deadlck_symtm = false;
                    // do this on per vnet basis

                    deadlck_symtm = get_net_ptr()->chk_deadlck_symptm(m_id, 0);

                    if (deadlck_symtm) {
                        /*initiate bailout sequence and then proceed normally*/
                        #if (MY_PRINT)
                        cout << "----initiating bail_out sequence----" << endl;
                        #endif
                        get_net_ptr()->bail_out(m_id);
                        get_net_ptr()->m_total_bailout++;
                    } else {
                        // proceed normally via doSwap()
                    }
                } else {
                    // this condition is also true when
                    // 'm_inj_single_vnet == 0'
                    assert(get_net_ptr()->m_no_is_swap == 1);
                }
                // If the result of GarnetNetwork::doSwap()
                // is not NULL then remove the flit from inport by doing
                // getTopFlit
                #if (MY_PRINT)
                    cout << "Upstream Router-id: " << m_id <<" swap_ptr.inport: "\
                         << swap_ptr.inport << endl;
                    cout << "swap_ptr.vcid: " << swap_ptr.vcid <<" swap_ptr.inport_dirn: "\
                         << swap_ptr.inport_dirn << endl;
                    cout << "Candidate flit of upstream router: " << endl;
                    cout << *m_input_unit[swap_ptr.inport]->peekTopFlit(swap_ptr.vcid)
                         << endl;
                #endif
                // taking care of the case when the flit itself is RoutedSwap
                // and is pointed by the swap_ptr.
                if (m_input_unit[swap_ptr.inport]->peekTopFlit(swap_ptr.vcid)\
                    ->get_RoutedSwap()) {
                    // here the swapped flit is trying to make forward progress
                    // via swaps. set the flag, which will be used later to
                    // clear 'is_swap' bit of this router and
                    // keep 'routedSwap' bit in the flit high.
                    #if (MY_PRINT)
                        cout << "'routedSwap' flit is trying to make forward"\
                                 "progress via Swap!" << endl;
                    #endif
                    if (get_net_ptr()->m_no_is_swap == 0) {
                        assert(this->is_swap);
                    }
                    this->send_routedSwap = true; // setting the flag

                }
                // because we have made sure swap_ptr always points to non-empty
                // vcid
                flit* flit_t = get_net_ptr()->doSwap(
                                            m_input_unit[swap_ptr.inport]->\
                                            peekTopFlit(swap_ptr.vcid), m_id);

                // by upstream router:
                // 1. Recompute the route (happens in doSwap_enqueue())
                // 2. insert this flit in the router
                if (flit_t != NULL) {
                    // remove the flit from the input port of that input unit...
                    m_input_unit[swap_ptr.inport]->getTopFlit(swap_ptr.vcid);
                    #if (MY_PRINT)
                        cout << "Mis-routed flit we got from downstream"\
                             << "router: " << endl;
                        cout << *flit_t << endl;
                        cout <<"Router-id: " << m_id <<
                            " swap_ptr.inport_dirn: " <<
                            swap_ptr.inport_dirn <<
                            " swap_ptr.vcid: " << swap_ptr.vcid <<
                            endl;
                    #endif
                    doSwap_enqueue(flit_t, m_input_unit[swap_ptr.inport]->\
                                    get_direction(), swap_ptr.inport, swap_ptr.vcid);
                    #if (MY_PRINT)
                        cout << "<<<<<<Completed the swap successfully>>>>>"\
                            << endl;
                    #endif
                    // update the stats
                    get_net_ptr()->increment_total_swaps();

                    if (this->send_routedSwap) {
                        if (get_net_ptr()->m_no_is_swap == 0) {
                            assert(this->is_swap);
                        }
                        this->send_routedSwap = false;
                        this->is_swap = false;
                        // the flit has made forward progress using swaps from
                        // downstream router
                        get_net_ptr()->m_total_routedSwaps++;
                    }

                }
                else {
                    // Swap is not possible because either:
                    // 1. swap_ptr is sending it flit for Local outport out
                    // 2. Downstream router's inport is empty.. the flit will
                    // then go by usual SwitchArbiteration mechanism.
                    // 3. Downstream Router's 'is_swap' bit is high
                    // 4. Downstream Router's mis-route flit has Local outport

                    //Therfore if 'send_routedSwap' is set before clear it here.
                    if (this->send_routedSwap) {
                        if (get_net_ptr()->m_no_is_swap == 0) {
                            assert(this->is_swap);
                        }
                        this->send_routedSwap = false;
                    }
                    get_net_ptr()->m_total_failed_swaps++;
                }
            }
            else {
                // swap is not possible because there is no flit in the
                // input port's vc-0 in the upstream router to swap with
            }

            // THis is the upstream router irrespective of completing/not-complting
            // the swap---update the direction of swap_ptr.
            movSwapPtr(); // only move swap_ptr when it's valid
        }
    }

    // check for incoming flits
    for (int inport = 0; inport < m_input_unit.size(); inport++) {
        m_input_unit[inport]->wakeup();
    }

    // check for incoming credits
    // Note: the credit update is happening before SA
    // buffer turnaround time =
    //     credit traversal (1-cycle) + SA (1-cycle) + Link Traversal (1-cycle)
    // if we want the credit update to take place after SA, this loop should
    // be moved after the SA request
    for (int outport = 0; outport < m_output_unit.size(); outport++) {
        // updates the credits and mark vc state to be idle if need be.
        m_output_unit[outport]->wakeup();
    }

    // Switch Allocation
    m_sw_alloc->wakeup();

    // Switch Traversal
    m_switch->wakeup();

    #if (MY_PRINT)
        cout << "----[end of Router"<<m_id<<"::wakeup()]vc-state-dump----------" << endl;
        vcStateDump();
    #endif
}


void
Router::movSwapPtr() {
    // THis is the upstream router irrespective of completing/not-complting
    // the swap---update the direction of swap_ptr.
    #if (MY_PRINT)
        cout << "Router-id: " << m_id << endl;
        cout <<"Before movSwapPtr(): swap_ptr.inport: " << swap_ptr.inport  \
            <<" swap_ptr.vcid: " << swap_ptr.vcid \
            << " swap_ptr.inport_dirn: " << swap_ptr.inport_dirn \
            << " swap_ptr.valid: " << swap_ptr.valid << endl;
    #endif
    // int num_vnet = m_virtual_networks;

    // int num_vcs = get_num_vcs();
    int itr;
    int invc = -1;

    for (int inport = 0; inport < get_num_inports(); inport++) {
        // simpler swap_ptr movement logic
        swap_ptr.inport++;
        if (swap_ptr.inport == get_num_inports())
          swap_ptr.inport = 0; // looping over
        if (get_net_ptr()->get_whichToSwap() == DISABLE_LOCAL_SWAP_) {
            if (m_input_unit[swap_ptr.inport]->get_direction() == "Local")
                continue;
        }

        for (itr= 0; itr < get_num_vcs(); itr++) {
            invc = swap_ptr.vcid++;
            if (invc >= m_num_vcs) {
                invc = 0;
            }
            if((m_input_unit[swap_ptr.inport]->vc_isEmpty(invc) == false)) {
                goto jmp;
            }
            else {
                continue;
            }
        }
    }

   jmp:
    if ( m_input_unit[swap_ptr.inport]->vc_isEmpty(invc) == false ) {
        swap_ptr.inport_dirn = m_input_unit[swap_ptr.inport]->get_direction();
        swap_ptr.vcid = invc;
        swap_ptr.valid = true;
    }
    else {
        // make swap_ptr invalid
        swap_ptr.vcid = -1;
        swap_ptr.valid = false;
        swap_ptr.inport = -1;
    }

    // assert that, if 'swap_ptr' is valid then it's pointing to a non-empty VC.
    if (swap_ptr.valid) {
        assert(swap_ptr.vcid >=0 );
        assert(m_input_unit[swap_ptr.inport]->vc_isEmpty(swap_ptr.vcid) == false);
        // update the dirn of swap_ptr here.
        // At this point swap_ptr.inport should be pointing to the next input-unit
        // of upstream router. Therefore, set that direction in the swap_ptr.
        swap_ptr.inport_dirn = m_routing_unit->m_inports_idx2dirn[swap_ptr.inport];
        assert(swap_ptr.inport_dirn.length() != 0); // shouldn't be an empty string

        if (get_net_ptr()->get_whichToSwap() == DISABLE_LOCAL_SWAP_)
            assert(swap_ptr.inport_dirn != "Local"); // should not point to a local inport

        #if (MY_PRINT)
            cout << "After movSwapPtr(): swap_ptr.inport: " << swap_ptr.inport << \
            " swap_ptr.inport_dirn: " << swap_ptr.inport_dirn << endl;
        #endif
    } else {
        // scanRouter();
        // assert(m_input_unit[swap_ptr.inport]->vc_isEmpty(swap_ptr.vcid));
         #if (MY_PRINT)
            cout << "There's no non-vacant inport present in the current router..."\
                    "making the swap_ptr invalid. It will be valid back again in"\
                    "InputUnit.cc" << endl;
         #endif
         // switching off the swap_ptr.vcid and swap_ptr.inport
        swap_ptr.vcid = -1;
        swap_ptr.inport = -1;
    }
    return;

}

void
Router::scanRouter() {
    cout << "**********************************************" << endl;
    cout << "--------" << endl;
    cout << "Router_id: " << get_id() << endl;;
    cout << "is_swap: " << is_swap << endl;
    cout << "swap_ptr.valid: " << swap_ptr.valid << endl;
    cout << "swap_ptr.inport: " << swap_ptr.inport << endl;
    cout << "swap_ptr.inport_dirn: " << swap_ptr.inport_dirn << endl;
    for (int inport = 0; inport < get_num_inports(); inport++) {
        // print here the inport ID and flit in that inport...
        cout << "inport: " << inport << " direction: " << get_inputUnit_ref()[inport]\
                                                                ->get_direction() << endl;
        assert(inport == get_inputUnit_ref()[inport]->get_id());
        if (get_inputUnit_ref()[inport]->vc_isEmpty(0)) {
            cout << "inport is empty" << endl;
        } else {
            cout << "flit info in this inport:" << endl;
            cout << *(get_inputUnit_ref()[inport]->peekTopFlit(0)) << endl;
        }
    }


}

bool
Router::outportNotLocal() {
    // this will check the flit sitting at
    // inport-vc pointed by
    // the swap_ptr doesn't have local
    // outport
    // and should not be empty... in both cases
    // upstream router is not allowed to make the
    // swap
    int inport = swap_ptr.inport;
    int vcid = swap_ptr.vcid;

    if (get_net_ptr()->m_occupancy_swap > 0) {
        // calculate the occupany of the inport
        // pointed by this swap_pointer; if it is
        // greater then let the code fall through
        // otherwise return 'false'
        PortDirection dirn_ =
                    m_input_unit[swap_ptr.inport]->get_direction();
        int free_vc =
                m_input_unit[swap_ptr.inport]->get_numFreeVC(dirn_);
        double occupancy_ = (1.0 - (free_vc/(double)m_vc_per_vnet))*100;
        if ( occupancy_ < (float)(get_net_ptr()->m_occupancy_swap) ) {
            return false;
        }
    }
    if (m_input_unit[inport]->vc_isEmpty(vcid)) {
        #if (MY_PRINT)
        cout << "Cannot Swap because this upstream router's inport"\
                " pointed by swap_ptr is empty!! " << endl;
        #endif
        get_net_ptr()->m_total_failed_upstream_empty++;
        return false;
    }
    int outport = m_input_unit[inport]->peekTopFlit(vcid)->get_outport();
    PortDirection outport_dir = m_input_unit[inport]->\
                                peekTopFlit(vcid)->get_outport_dir();
    // sanity check:
    assert(outport_dir.length() != 0);
    assert(m_output_unit[outport]->get_direction() == outport_dir);

    if (outport_dir == "Local") {
        #if (MY_PRINT)
            cout << "Cannot swap because outport of the flit is pointing to"\
                    " local port of this router" << endl;
        #endif
        get_net_ptr()->m_total_failed_upstream_localOuport++;
        return false;
    } else
        return true;

}

flit*
Router::doSwap(PortDirection inport_dirn, int vcid)
{
    // this should have been taken care of by caller.
    assert(inport_dirn != "Local");
    // additional check.. only swap when all the
    // vcs for the given inport are NOT empty..
    int inport;
    inport = m_routing_unit->m_inports_dirn2idx[inport_dirn];

    if (get_net_ptr()->m_no_is_swap == 0) {
        if (is_swap == false) {
            // using the 'vcid' if this router
             if (m_input_unit[inport]->vc_isEmpty(vcid)) {
                get_net_ptr()->m_total_failed_downstream_empty++;
                return NULL;
             } else {
                flit* flit_t = m_input_unit[inport]->peekTopFlit(vcid);
                // do not return the flit whoes outport is "Local"
                if (flit_t->get_outport_dir() == "Local") {
                    get_net_ptr()->m_total_failed_downstream_localOutport;
                    return NULL;
                } else {
                    // now you can remove the flit...
                    m_input_unit[inport]->getTopFlit(vcid); // remove the flit
                    return (flit_t);
                }
            }
        }
        else {
            return NULL;
        }
    }
    else if ( get_net_ptr()->m_no_is_swap == 1 ) {
         if (m_input_unit[inport]->vc_isEmpty(vcid)) {
            get_net_ptr()->m_total_failed_downstream_empty++;
            #if (MY_PRINT)
                cout << "Declining SWAP because the vcid is empty" << endl;
            #endif
            return NULL;
         } else {
            flit* flit_t = m_input_unit[inport]->peekTopFlit(vcid);
            // do not return the flit whoes outport is "Local"
            if (flit_t->get_outport_dir() == "Local") {
                get_net_ptr()->m_total_failed_downstream_localOutport;
                #if (MY_PRINT)
                    cout << "Declining SWAP because the flit is at its destination" << endl;
                #endif
                return NULL;
            } else {
                // now you can remove the flit...
                m_input_unit[inport]->getTopFlit(vcid); // remove the flit
                return (flit_t);
            }
        }
    }
    else {
        assert(0);
        return NULL;
    }
}

void
Router::doSwap_enqueue(flit * flit_t, PortDirection inport_dirn,
int inport_id, int vcid)
{
    if (get_net_ptr()->get_whichToSwap() == ENABLE_LOCAL_SWAP_) {
        if (inport_id == -1) // called by upstream router
            assert(inport_dirn != "Local");
    } else
        assert(inport_dirn != "Local");
    int outport =
        route_compute(flit_t->get_route(),
                      m_routing_unit->\
                      m_inports_dirn2idx[inport_dirn],
                      inport_dirn);
    flit_t->set_outport(outport);
    flit_t->set_outport_dir(m_routing_unit->\
                            m_outports_idx2dirn[outport]);
    // set this new vc in the flit (which is same as vcid of the upstream router)
    flit_t->set_vc(vcid);
    int vc = flit_t->get_vc();
    assert(vc != -1);
    if (inport_id == -1) {
        // This means we are enqueuing a "Routed" flit
        // in downstream router.
        inport_id = m_routing_unit->m_inports_dirn2idx[inport_dirn];
        assert(inport_id != -1);
    } else {

        if (get_net_ptr()->m_no_is_swap == 0) {
            assert(flit_t->get_RoutedSwap() == false);
        }
        assert(m_routing_unit->m_inports_idx2dirn[inport_id] ==
            inport_dirn);
    }
    m_input_unit[inport_id]->enqueue_flit(vc, flit_t);
    return;

}

bool
Router::checkSwapPtrValid() {
    #if (MY_PRINT)
    cout << "Router::checkSwapPtrValid  swap_ptr.valid: " << swap_ptr.valid <<
            " swap_ptr.vcid: " << swap_ptr.vcid << " swap_ptr.inport: " <<
            swap_ptr.inport << endl;
    #endif
    return (swap_ptr.valid);
}

void
Router::makeSwapPtrValid(PortDirection dirn, int vc) {
    #if (MY_PRINT)
        cout << "Router::makeSwapPtrValid(); Direction: " << dirn << endl;
    #endif
    assert(swap_ptr.valid == false);
    assert(swap_ptr.vcid == -1);
    swap_ptr.valid = true;
    swap_ptr.inport_dirn = dirn;
    swap_ptr.vcid = vc;
    assert(dirn != "Local");
    swap_ptr.inport = m_routing_unit->m_inports_dirn2idx[dirn];
    return;
}


void
Router::addInPort(PortDirection inport_dirn,
                  NetworkLink *in_link, CreditLink *credit_link)
{
    int port_num = m_input_unit.size();
    InputUnit *input_unit = new InputUnit(port_num, inport_dirn, this);

    input_unit->set_in_link(in_link);
    input_unit->set_credit_link(credit_link);
    in_link->setLinkConsumer(this);
    credit_link->setSourceQueue(input_unit->getCreditQueue());

    m_input_unit.push_back(input_unit);

    m_routing_unit->addInDirection(inport_dirn, port_num);
}

void
Router::addOutPort(PortDirection outport_dirn,
                   NetworkLink *out_link,
                   const NetDest& routing_table_entry, int link_weight,
                   CreditLink *credit_link)
{
    int port_num = m_output_unit.size();
    OutputUnit *output_unit = new OutputUnit(port_num, outport_dirn, this);

    output_unit->set_out_link(out_link);
    output_unit->set_credit_link(credit_link);
    credit_link->setLinkConsumer(this);
    out_link->setSourceQueue(output_unit->getOutQueue());

    m_output_unit.push_back(output_unit);

    m_routing_unit->addRoute(routing_table_entry);
    m_routing_unit->addWeight(link_weight);
    m_routing_unit->addOutDirection(outport_dirn, port_num);
}

PortDirection
Router::getOutportDirection(int outport)
{
    return m_output_unit[outport]->get_direction();
}

PortDirection
Router::getInportDirection(int inport)
{
    return m_input_unit[inport]->get_direction();
}

int
Router::route_compute(RouteInfo route, int inport, PortDirection inport_dirn)
{
    return m_routing_unit->outportCompute(route, inport, inport_dirn);
}

void
Router::grant_switch(int inport, flit *t_flit)
{
    m_switch->update_sw_winner(inport, t_flit);
}

void
Router::schedule_wakeup(Cycles time)
{
    // wake up after time cycles
    scheduleEvent(time);
}

std::string
Router::getPortDirectionName(PortDirection direction)
{
    // PortDirection is actually a string
    // If not, then this function should add a switch
    // statement to convert direction to a string
    // that can be printed out
    return direction;
}

void
Router::regStats()
{
    BasicRouter::regStats();

    m_buffer_reads
        .name(name() + ".buffer_reads")
        .flags(Stats::nozero)
    ;

    m_buffer_writes
        .name(name() + ".buffer_writes")
        .flags(Stats::nozero)
    ;

    m_crossbar_activity
        .name(name() + ".crossbar_activity")
        .flags(Stats::nozero)
    ;

    m_sw_input_arbiter_activity
        .name(name() + ".sw_input_arbiter_activity")
        .flags(Stats::nozero)
    ;

    m_sw_output_arbiter_activity
        .name(name() + ".sw_output_arbiter_activity")
        .flags(Stats::nozero)
    ;
}

void
Router::collateStats()
{
    for (int j = 0; j < m_virtual_networks; j++) {
        for (int i = 0; i < m_input_unit.size(); i++) {
            m_buffer_reads += m_input_unit[i]->get_buf_read_activity(j);
            m_buffer_writes += m_input_unit[i]->get_buf_write_activity(j);
        }
    }

    m_sw_input_arbiter_activity = m_sw_alloc->get_input_arbiter_activity();
    m_sw_output_arbiter_activity = m_sw_alloc->get_output_arbiter_activity();
    m_crossbar_activity = m_switch->get_crossbar_activity();
}

void
Router::resetStats()
{
    for (int j = 0; j < m_virtual_networks; j++) {
        for (int i = 0; i < m_input_unit.size(); i++) {
            m_input_unit[i]->resetStats();
        }
    }

    m_switch->resetStats();
    m_sw_alloc->resetStats();
}

void
Router::printFaultVector(ostream& out)
{
    int temperature_celcius = BASELINE_TEMPERATURE_CELCIUS;
    int num_fault_types = m_network_ptr->fault_model->number_of_fault_types;
    float fault_vector[num_fault_types];
    get_fault_vector(temperature_celcius, fault_vector);
    out << "Router-" << m_id << " fault vector: " << endl;
    for (int fault_type_index = 0; fault_type_index < num_fault_types;
         fault_type_index++) {
        out << " - probability of (";
        out <<
        m_network_ptr->fault_model->fault_type_to_string(fault_type_index);
        out << ") = ";
        out << fault_vector[fault_type_index] << endl;
    }
}

void
Router::printAggregateFaultProbability(std::ostream& out)
{
    int temperature_celcius = BASELINE_TEMPERATURE_CELCIUS;
    float aggregate_fault_prob;
    get_aggregate_fault_probability(temperature_celcius,
                                    &aggregate_fault_prob);
    out << "Router-" << m_id << " fault probability: ";
    out << aggregate_fault_prob << endl;
}

uint32_t
Router::functionalWrite(Packet *pkt)
{
    uint32_t num_functional_writes = 0;
    num_functional_writes += m_switch->functionalWrite(pkt);

    for (uint32_t i = 0; i < m_input_unit.size(); i++) {
        num_functional_writes += m_input_unit[i]->functionalWrite(pkt);
    }

    for (uint32_t i = 0; i < m_output_unit.size(); i++) {
        num_functional_writes += m_output_unit[i]->functionalWrite(pkt);
    }

    return num_functional_writes;
}

Router *
GarnetRouterParams::create()
{
    return new Router(this);
}
