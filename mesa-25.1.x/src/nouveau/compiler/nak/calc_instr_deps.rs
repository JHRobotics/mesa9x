// Copyright © 2022 Collabora, Ltd.
// SPDX-License-Identifier: MIT

use crate::api::{GetDebugFlags, DEBUG};
use crate::ir::*;
use crate::reg_tracker::RegTracker;

use std::cmp::max;
use std::collections::{HashMap, HashSet};
use std::slice;

#[derive(Clone)]
enum RegUse<T: Clone> {
    None,
    Write(T),
    Reads(Vec<T>),
}

impl<T: Clone> RegUse<T> {
    pub fn deps(&self) -> &[T] {
        match self {
            RegUse::None => &[],
            RegUse::Write(dep) => slice::from_ref(dep),
            RegUse::Reads(deps) => &deps[..],
        }
    }

    pub fn clear(&mut self) -> Self {
        std::mem::replace(self, RegUse::None)
    }

    pub fn clear_write(&mut self) -> Self {
        if matches!(self, RegUse::Write(_)) {
            std::mem::replace(self, RegUse::None)
        } else {
            RegUse::None
        }
    }

    pub fn add_read(&mut self, dep: T) -> Self {
        match self {
            RegUse::None => {
                *self = RegUse::Reads(vec![dep]);
                RegUse::None
            }
            RegUse::Write(_) => {
                std::mem::replace(self, RegUse::Reads(vec![dep]))
            }
            RegUse::Reads(reads) => {
                reads.push(dep);
                RegUse::None
            }
        }
    }

    pub fn set_write(&mut self, dep: T) -> Self {
        std::mem::replace(self, RegUse::Write(dep))
    }
}

struct DepNode {
    read_dep: Option<usize>,
    first_wait: Option<(usize, usize)>,
}

struct DepGraph {
    deps: Vec<DepNode>,
    instr_deps: HashMap<(usize, usize), (usize, usize)>,
    instr_waits: HashMap<(usize, usize), Vec<usize>>,
    active: HashSet<usize>,
}

impl DepGraph {
    pub fn new() -> Self {
        Self {
            deps: Vec::new(),
            instr_deps: HashMap::new(),
            instr_waits: HashMap::new(),
            active: HashSet::new(),
        }
    }

    fn add_new_dep(&mut self, read_dep: Option<usize>) -> usize {
        let dep = self.deps.len();
        self.deps.push(DepNode {
            read_dep: read_dep,
            first_wait: None,
        });
        dep
    }

    pub fn add_instr(&mut self, block_idx: usize, ip: usize) -> (usize, usize) {
        let rd = self.add_new_dep(None);
        let wr = self.add_new_dep(Some(rd));
        self.instr_deps.insert((block_idx, ip), (rd, wr));
        (rd, wr)
    }

    pub fn add_signal(&mut self, dep: usize) {
        self.active.insert(dep);
    }

    pub fn add_waits(
        &mut self,
        block_idx: usize,
        ip: usize,
        mut waits: Vec<usize>,
    ) {
        for dep in &waits {
            // A wait on a write automatically waits on the read.  By removing
            // it from the active set here we ensure that we don't record any
            // duplicate write/read waits in the retain below.
            if let Some(rd) = &self.deps[*dep].read_dep {
                self.active.remove(rd);
            }
        }

        waits.retain(|dep| {
            let node = &mut self.deps[*dep];
            if let Some(wait) = node.first_wait {
                // Someone has already waited on this dep
                debug_assert!(!self.active.contains(dep));
                debug_assert!((block_idx, ip) >= wait);
                false
            } else if !self.active.contains(dep) {
                // Even if it doesn't have a use, it may still be deactivated.
                // This can happen if we depend the the destination before any
                // of its sources.
                false
            } else {
                self.deps[*dep].first_wait = Some((block_idx, ip));
                self.active.remove(dep);
                true
            }
        });

        // Sort for stability.  The list of waits may come from a HashSet (see
        // add_barrier()) and so it's not guaranteed stable across Rust
        // versions.  This also ensures that everything always waits on oldest
        // dependencies first.
        waits.sort();

        let _old = self.instr_waits.insert((block_idx, ip), waits);
        debug_assert!(_old.is_none());
    }

    pub fn add_barrier(&mut self, block_idx: usize, ip: usize) {
        let waits = self.active.iter().cloned().collect();
        self.add_waits(block_idx, ip, waits);
        debug_assert!(self.active.is_empty());
    }

    pub fn dep_is_waited_after(
        &self,
        dep: usize,
        block_idx: usize,
        ip: usize,
    ) -> bool {
        if let Some(wait) = self.deps[dep].first_wait {
            wait > (block_idx, ip)
        } else {
            false
        }
    }

    pub fn get_instr_deps(
        &self,
        block_idx: usize,
        ip: usize,
    ) -> (usize, usize) {
        *self.instr_deps.get(&(block_idx, ip)).unwrap()
    }

    pub fn get_instr_waits(&self, block_idx: usize, ip: usize) -> &[usize] {
        if let Some(waits) = self.instr_waits.get(&(block_idx, ip)) {
            &waits[..]
        } else {
            &[]
        }
    }
}

struct BarAlloc {
    num_bars: u8,
    bar_dep: [usize; 6],
}

impl BarAlloc {
    pub fn new() -> BarAlloc {
        BarAlloc {
            num_bars: 6,
            bar_dep: [usize::MAX; 6],
        }
    }

    pub fn bar_is_free(&self, bar: u8) -> bool {
        debug_assert!(bar < self.num_bars);
        self.bar_dep[usize::from(bar)] == usize::MAX
    }

    pub fn set_bar_dep(&mut self, bar: u8, dep: usize) {
        debug_assert!(self.bar_is_free(bar));
        self.bar_dep[usize::from(bar)] = dep;
    }

    pub fn free_bar(&mut self, bar: u8) {
        debug_assert!(!self.bar_is_free(bar));
        self.bar_dep[usize::from(bar)] = usize::MAX;
    }

    pub fn try_find_free_bar(&self) -> Option<u8> {
        for bar in 0..self.num_bars {
            if self.bar_is_free(bar) {
                return Some(bar);
            }
        }
        None
    }

    pub fn free_some_bar(&mut self) -> u8 {
        // Get the oldest by looking for the one with the smallest dep
        let mut bar = 0;
        for b in 1..self.num_bars {
            if self.bar_dep[usize::from(b)] < self.bar_dep[usize::from(bar)] {
                bar = b;
            }
        }
        self.free_bar(bar);
        bar
    }

    pub fn get_bar_for_dep(&self, dep: usize) -> Option<u8> {
        for bar in 0..self.num_bars {
            if self.bar_dep[usize::from(bar)] == dep {
                return Some(bar);
            }
        }
        None
    }
}

fn assign_barriers(f: &mut Function, sm: &dyn ShaderModel) {
    let mut uses = Box::new(RegTracker::new_with(&|| RegUse::None));
    let mut deps = DepGraph::new();

    for (bi, b) in f.blocks.iter().enumerate() {
        for (ip, instr) in b.instrs.iter().enumerate() {
            if instr.is_branch() {
                deps.add_barrier(bi, ip);
            } else {
                // Execution predicates are handled immediately and we don't
                // need barriers for them, regardless of whether or not it's a
                // fixed-latency instruction.
                let mut waits = Vec::new();
                uses.for_each_instr_pred_mut(instr, |u| {
                    let u = u.clear_write();
                    waits.extend_from_slice(u.deps());
                });

                if sm.op_needs_scoreboard(&instr.op) {
                    let (rd, wr) = deps.add_instr(bi, ip);
                    uses.for_each_instr_src_mut(instr, |_, u| {
                        // Only mark a dep as signaled if we actually have
                        // something that shows up in the register file as
                        // needing scoreboarding
                        deps.add_signal(rd);
                        let u = u.add_read(rd);
                        waits.extend_from_slice(u.deps());
                    });
                    uses.for_each_instr_dst_mut(instr, |_, u| {
                        // Only mark a dep as signaled if we actually have
                        // something that shows up in the register file as
                        // needing scoreboarding
                        deps.add_signal(wr);
                        let u = u.set_write(wr);
                        for dep in u.deps() {
                            // Don't wait on ourselves
                            if *dep != rd {
                                waits.push(*dep);
                            }
                        }
                    });
                } else {
                    // Delays will cover us here.  We just need to make sure
                    // that we wait on any uses that we consume.
                    uses.for_each_instr_src_mut(instr, |_, u| {
                        let u = u.clear_write();
                        waits.extend_from_slice(u.deps());
                    });
                    uses.for_each_instr_dst_mut(instr, |_, u| {
                        let u = u.clear();
                        waits.extend_from_slice(u.deps());
                    });
                }
                deps.add_waits(bi, ip, waits);
            }
        }
    }

    let mut bars = BarAlloc::new();

    for (bi, b) in f.blocks.iter_mut().enumerate() {
        for (ip, instr) in b.instrs.iter_mut().enumerate() {
            let mut wait_mask = 0_u8;
            for dep in deps.get_instr_waits(bi, ip) {
                if let Some(bar) = bars.get_bar_for_dep(*dep) {
                    wait_mask |= 1 << bar;
                    bars.free_bar(bar);
                }
            }
            instr.deps.add_wt_bar_mask(wait_mask);

            if instr.needs_yield() {
                instr.deps.set_yield(true);
            }

            if !sm.op_needs_scoreboard(&instr.op) {
                continue;
            }

            let (rd_dep, wr_dep) = deps.get_instr_deps(bi, ip);
            if deps.dep_is_waited_after(rd_dep, bi, ip) {
                let rd_bar = bars.try_find_free_bar().unwrap_or_else(|| {
                    let bar = bars.free_some_bar();
                    instr.deps.add_wt_bar(bar);
                    bar
                });
                bars.set_bar_dep(rd_bar, rd_dep);
                instr.deps.set_rd_bar(rd_bar);
            }
            if deps.dep_is_waited_after(wr_dep, bi, ip) {
                let wr_bar = bars.try_find_free_bar().unwrap_or_else(|| {
                    let bar = bars.free_some_bar();
                    instr.deps.add_wt_bar(bar);
                    bar
                });
                bars.set_bar_dep(wr_bar, wr_dep);
                instr.deps.set_wr_bar(wr_bar);
            }
        }
    }
}

fn calc_delays(f: &mut Function, sm: &dyn ShaderModel) -> u32 {
    let mut min_num_static_cycles = 0;
    for b in f.blocks.iter_mut().rev() {
        let mut cycle = 0_u32;

        // Vector mapping IP to start cycle
        let mut instr_cycle = Vec::new();
        instr_cycle.resize(b.instrs.len(), 0_u32);

        // Maps registers to RegUse<ip, src_dst_idx>.  Predicates are
        // represented by  src_idx = usize::MAX.
        let mut uses: Box<RegTracker<RegUse<(usize, usize)>>> =
            Box::new(RegTracker::new_with(&|| RegUse::None));

        // Map from barrier to last waited cycle
        let mut bars = [0_u32; 6];

        for ip in (0..b.instrs.len()).rev() {
            let instr = &b.instrs[ip];
            let mut min_start = cycle + sm.exec_latency(&instr.op);
            if let Some(bar) = instr.deps.rd_bar() {
                min_start = max(min_start, bars[usize::from(bar)] + 2);
            }
            if let Some(bar) = instr.deps.wr_bar() {
                min_start = max(min_start, bars[usize::from(bar)] + 2);
            }
            uses.for_each_instr_dst_mut(instr, |i, u| match u {
                RegUse::None => {
                    // We don't know how it will be used but it may be used in
                    // the next block so we need at least assume the maximum
                    // destination latency from the end of the block.
                    let s = sm.worst_latency(&instr.op, i);
                    min_start = max(min_start, s);
                }
                RegUse::Write((w_ip, w_dst_idx)) => {
                    let s = instr_cycle[*w_ip]
                        + sm.waw_latency(
                            &instr.op,
                            i,
                            !instr.pred.pred_ref.is_none(),
                            &b.instrs[*w_ip].op,
                            *w_dst_idx,
                        );
                    min_start = max(min_start, s);
                }
                RegUse::Reads(reads) => {
                    for (r_ip, r_src_idx) in reads {
                        let c = instr_cycle[*r_ip];
                        let s = if *r_src_idx == usize::MAX {
                            c + sm.paw_latency(&instr.op, i)
                        } else {
                            c + sm.raw_latency(
                                &instr.op,
                                i,
                                &b.instrs[*r_ip].op,
                                *r_src_idx,
                            )
                        };
                        min_start = max(min_start, s);
                    }
                }
            });
            uses.for_each_instr_src_mut(instr, |i, u| match u {
                RegUse::None => (),
                RegUse::Write((w_ip, w_dst_idx)) => {
                    let s = instr_cycle[*w_ip]
                        + sm.war_latency(
                            &instr.op,
                            i,
                            &b.instrs[*w_ip].op,
                            *w_dst_idx,
                        );
                    min_start = max(min_start, s);
                }
                RegUse::Reads(_) => (),
            });

            let instr = &mut b.instrs[ip];

            let delay = min_start - cycle;
            let delay = delay.max(MIN_INSTR_DELAY.into()).try_into().unwrap();
            instr.deps.set_delay(delay);

            instr_cycle[ip] = min_start;

            // Set the writes before adding the reads
            // as we are iterating backwards through instructions.
            uses.for_each_instr_dst_mut(instr, |i, c| {
                c.set_write((ip, i));
            });
            uses.for_each_instr_pred_mut(instr, |c| {
                c.add_read((ip, usize::MAX));
            });
            uses.for_each_instr_src_mut(instr, |i, c| {
                c.add_read((ip, i));
            });
            for (bar, c) in bars.iter_mut().enumerate() {
                if instr.deps.wt_bar_mask & (1 << bar) != 0 {
                    *c = min_start;
                }
            }

            cycle = min_start;
        }
        min_num_static_cycles += cycle;
    }

    f.map_instrs(|mut instr, _| {
        if instr.deps.delay > MAX_INSTR_DELAY {
            let mut delay = instr.deps.delay - MAX_INSTR_DELAY;
            instr.deps.set_delay(MAX_INSTR_DELAY);
            let mut instrs = vec![instr];
            while delay > 0 {
                let mut nop = Instr::new_boxed(OpNop { label: None });
                nop.deps.set_delay(delay.min(MAX_INSTR_DELAY));
                delay -= nop.deps.delay;
                instrs.push(nop);
            }
            MappedInstrs::Many(instrs)
        } else if matches!(instr.op, Op::SrcBar(_)) {
            instr.op = Op::Nop(OpNop { label: None });
            MappedInstrs::One(instr)
        } else if sm.exec_latency(&instr.op) > 1 {
            // It's unclear exactly why but the blob inserts a Nop with a delay
            // of 2 after every instruction which has an exec latency.  Perhaps
            // it has something to do with .yld?  In any case, the extra 2
            // cycles aren't worth the chance of weird bugs.
            let mut nop = Instr::new_boxed(OpNop { label: None });
            nop.deps.set_delay(2);
            MappedInstrs::Many(vec![instr, nop])
        } else {
            MappedInstrs::One(instr)
        }
    });

    min_num_static_cycles
}

impl Shader<'_> {
    pub fn assign_deps_serial(&mut self) {
        for f in &mut self.functions {
            for b in &mut f.blocks.iter_mut().rev() {
                let mut wt = 0_u8;
                for instr in &mut b.instrs {
                    if matches!(&instr.op, Op::Bar(_))
                        || matches!(&instr.op, Op::BClear(_))
                        || matches!(&instr.op, Op::BSSy(_))
                        || matches!(&instr.op, Op::BSync(_))
                    {
                        instr.deps.set_yield(true);
                    } else if instr.is_branch() {
                        instr.deps.add_wt_bar_mask(0x3f);
                    } else {
                        instr.deps.add_wt_bar_mask(wt);
                        if instr.dsts().len() > 0 {
                            instr.deps.set_wr_bar(0);
                            wt |= 1 << 0;
                        }
                        if !instr.pred.pred_ref.is_none()
                            || instr.srcs().len() > 0
                        {
                            instr.deps.set_rd_bar(1);
                            wt |= 1 << 1;
                        }
                    }
                }
            }
        }
    }

    pub fn calc_instr_deps(&mut self) {
        if DEBUG.serial() {
            self.assign_deps_serial();
        } else {
            let mut min_num_static_cycles = 0;
            for f in &mut self.functions {
                assign_barriers(f, self.sm);
                min_num_static_cycles += calc_delays(f, self.sm);
            }

            if DEBUG.cycles() {
                // This is useful for debugging differences in the scheduler
                // cycle count model and the calc_delays() model.  However, it
                // isn't totally valid since assign_barriers() can add extra
                // dependencies for barrier re-use and those may add cycles.
                // The chances of it doing this are low, thanks to our LRU
                // allocation strategy, but it's still not an assert we want
                // running in production.
                assert!(self.info.num_static_cycles >= min_num_static_cycles);
            }
        }
    }
}
