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


#include "mem/ruby/network/garnet2.0/InputUnit.hh"

#include "base/stl_helpers.hh"
#include "debug/RubyNetwork.hh"
#include "mem/ruby/network/garnet2.0/Credit.hh"
#include "mem/ruby/network/garnet2.0/Router.hh"
#include "mem/ruby/network/garnet2.0/RoutingUnit.hh"

using namespace std;
using m5::stl_helpers::deletePointers;

// constructor
// called at the time of topology creation in the very begning of
// simulation
InputUnit::InputUnit(int id, PortDirection direction, Router *router)
            : Consumer(router)
{
    m_id = id;
    m_direction = direction;
    m_router = router;
    m_num_vcs = m_router->get_num_vcs();
    m_vc_per_vnet = m_router->get_vc_per_vnet();

    m_num_buffer_reads.resize(m_num_vcs/m_vc_per_vnet);
    m_num_buffer_writes.resize(m_num_vcs/m_vc_per_vnet);
    for (int i = 0; i < m_num_buffer_reads.size(); i++) {
        m_num_buffer_reads[i] = 0;
        m_num_buffer_writes[i] = 0;
    }

    creditQueue = new flitBuffer();
    // Instantiating the virtual channels
    m_vcs.resize(m_num_vcs);
    for (int i=0; i < m_num_vcs; i++) {
        m_vcs[i] = new VirtualChannel(i);
    }
}

InputUnit::~InputUnit()
{
    delete creditQueue;
    deletePointers(m_vcs);
}

/*
 * The InputUnit wakeup function reads the input flit from its input link.
 * Each flit arrives with an input VC.
 * For HEAD/HEAD_TAIL flits, performs route computation,
 * and updates route in the input VC.
 * The flit is buffered for (m_latency - 1) cycles in the input VC
 * and marked as valid for SwitchAllocation starting that cycle.
 *
 */

void
InputUnit::wakeup()
{
    flit *t_flit;
    if (m_in_link->isReady(m_router->curCycle())) {

        t_flit = m_in_link->consumeLink();
        int vc = t_flit->get_vc();
        // assert(vc < m_vc_per_vnet);
        // assert(vc != -1);
        t_flit->increment_hops(); // for stats
        #if (MY_PRINT)
            cout << "InputUnit::wakeup()--- m_id: " << m_id << endl;
            cout << "InputUnit::wakeup()--- direction: " << m_direction << endl;
            cout << "InputUnit::wakeup()--- t_flit->get_vc():  " << vc << endl;
        #endif

        if ((t_flit->get_type() == HEAD_) ||
            (t_flit->get_type() == HEAD_TAIL_)) {
            #if (MY_PRINT)
                cout << "Setting input--VC: "<< vc <<" state to ACTIVE_ " \
                    << endl;
            #endif
            assert(m_vcs[vc]->get_state() == IDLE_);
            set_vc_active(vc, m_router->curCycle());

            // Route computation for this vc
            int outport = m_router->route_compute(t_flit->get_route(),
                m_id, m_direction);

            // you have computed the outport of this flit.. put it
            // the flit as well
            t_flit->set_outport(outport);
            // set the outport_dir as well
            PortDirection outdir1; // Could this be buggy?
            PortDirection outdir2;
            outdir1 =
                m_router->get_routingUnit_ref()->m_outports_idx2dirn[outport];
            outdir2 =
                m_router->getOutportDirection(outport); // this sounds right!
            // sanity
            assert(outdir1 == outdir2);
            t_flit->set_outport_dir(outdir2);
            // Make swap_ptr valid under conditions laid in the
            // function comment
            makeSwapPtrValid(t_flit); // gateway of making swapPtr valid()

            // Update output port in VC
            // All flits in this packet will use this output port
            // The output port field in the flit is updated after it wins SA
            // NOTE: but I have updated it above
//            grant_outport(vc, outport);

        }
        else {
            assert(0); // we do not come here
            assert(m_vcs[vc]->get_state() == ACTIVE_);
        }


        // Buffer the flit
        m_vcs[vc]->insertFlit(t_flit);

        int vnet = vc/m_vc_per_vnet;
        // number of writes same as reads
        // any flit that is written will be read only once
        m_num_buffer_writes[vnet]++;
        m_num_buffer_reads[vnet]++;

        Cycles pipe_stages = m_router->get_pipe_stages();
        if (pipe_stages == 1) {
            // 1-cycle router
            // Flit goes for SA directly
            t_flit->advance_stage(SA_, m_router->curCycle());
        } else {
            assert(pipe_stages > 1);
            // Router delay is modeled by making flit wait in buffer for
            // (pipe_stages cycles - 1) cycles before going for SA

            Cycles wait_time = pipe_stages - Cycles(1);
            t_flit->advance_stage(SA_, m_router->curCycle() + wait_time);

            // Wakeup the router in that cycle to perform SA
            m_router->schedule_wakeup(Cycles(wait_time));
        }
    }
}


/*
Every InputUnit has pointer to Router to which it
belongs, access functions of Router modifying swap_ptr
from there
If already valid return;
else, if the flit is in
North;East;West;South <--this inputUnit dir
and has the request for outport
North;East;West;South;: idea is to
make 'forward' progress
and not let 'swap_ptr' point to 'Local' dirn ever
make it valid here
TODO: (movSwapPtr() should have this logic):
If the InputUnit is empty make the Swap_ptr Invalid
*/
void
InputUnit::makeSwapPtrValid(flit* t_flit) {
    // How to access the swap_ptr
    if (get_router()->checkSwapPtrValid()) {
        #if (MY_PRINT)
            cout << "Router-id: "<< get_router()->get_id() <<"; Flit arrived at Input-Unit: " \
                << this->m_direction <<"; but SwapPtr is valid in dir: " \
                << get_router()->swap_ptr.inport_dirn <<"; SwapPtr vcid: " \
                << get_router()->swap_ptr.vcid << endl;
        #endif
        return;
    }
    else {
        #if (MY_PRINT)
            cout << "Router-id: " << get_router()->get_id() << "; Flit arrived at Input-Unit:" \
                << this->m_direction << "; but 'SwapPtr' is invalid" << endl;
            cout << "InputUnit direction: " << this->m_direction <<
                " t_flit->get_outport_dir(): " <<
                t_flit->get_outport_dir() << endl;
            #endif

        // NOTE: Causing heavy perf-penatlty
        // we can also make swap_ptr point to Local Port if 'ENABLE_LOCAL_SWAP_' is true
        if (((this->m_direction == "North") || (this->m_direction == "East") ||
            (this->m_direction == "West") || (this->m_direction == "South")) &&
            ((t_flit->get_outport_dir() == "North") ||
             (t_flit->get_outport_dir() == "East") ||
             (t_flit->get_outport_dir() == "West") ||
             (t_flit->get_outport_dir() == "South"))) {
            // currently making swap_Ptr randomly valid;
            // in whichever inport dirn
            // flit comes first and taking from there to point to next inport
            // direction in SwitchAllocator stage
            get_router()->makeSwapPtrValid(this->m_direction, t_flit->get_vc());
            // if you are making swap_ptr valid;
            // also specify direction.
            #if (MY_PRINT)
                cout << "Router-id: " << get_router()->get_id() <<
                    "; Input-Unit: "<< this->m_id  <<
                    ";'SwapPtr is now valid in dirn: " <<
                    get_router()->swap_ptr.inport_dirn <<
                    "; SwapPtr now points to vcid: " <<
                    get_router()->swap_ptr.vcid << endl;
            #endif

        }
        return;
    }

}


// Send a credit back to upstream router for this VC.
// Called by SwitchAllocator when the flit in this VC wins the Switch.
void
InputUnit::increment_credit(int in_vc, bool free_signal, Cycles curTime)
{
    Credit *t_credit = new Credit(in_vc, free_signal, curTime);
    creditQueue->insert(t_credit);
    m_credit_link->scheduleEventAbsolute(m_router->clockEdge(Cycles(1)));
}


uint32_t
InputUnit::functionalWrite(Packet *pkt)
{
    uint32_t num_functional_writes = 0;
    for (int i=0; i < m_num_vcs; i++) {
        num_functional_writes += m_vcs[i]->functionalWrite(pkt);
    }

    return num_functional_writes;
}

void
InputUnit::resetStats()
{
    for (int j = 0; j < m_num_buffer_reads.size(); j++) {
        m_num_buffer_reads[j] = 0;
        m_num_buffer_writes[j] = 0;
    }
}
