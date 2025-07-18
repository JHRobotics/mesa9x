// Copyright © 2023 Collabora, Ltd.
// SPDX-License-Identifier: MIT

#![allow(unstable_name_collisions)]

use crate::api::{GetDebugFlags, DEBUG};
use crate::const_tracker::ConstTracker;
use crate::ir::*;
use crate::liveness::{
    BlockLiveness, LiveSet, Liveness, NextUseBlockLiveness, NextUseLiveness,
};

use compiler::bitset::BitSet;
use std::cell::RefCell;
use std::cmp::{max, Ordering, Reverse};
use std::collections::{BinaryHeap, HashMap, HashSet};

#[derive(Default)]
struct PhiDstMap {
    phi_ssa: HashMap<u32, SSAValue>,
    ssa_phi: HashMap<SSAValue, u32>,
}

impl PhiDstMap {
    fn new() -> PhiDstMap {
        Default::default()
    }

    fn add_phi_dst(&mut self, phi_idx: u32, dst: Dst) {
        let vec = dst.as_ssa().expect("Not an SSA destination");
        debug_assert!(vec.comps() == 1);
        self.phi_ssa.insert(phi_idx, vec[0]);
        self.ssa_phi.insert(vec[0], phi_idx);
    }

    pub fn from_block(block: &BasicBlock) -> PhiDstMap {
        let mut map = PhiDstMap::new();
        if let Some(phi) = block.phi_dsts() {
            for (idx, dst) in phi.dsts.iter() {
                map.add_phi_dst(*idx, *dst);
            }
        }
        map
    }

    fn get_phi_idx(&self, ssa: &SSAValue) -> Option<&u32> {
        self.ssa_phi.get(ssa)
    }

    fn get_dst_ssa(&self, phi_idx: &u32) -> Option<&SSAValue> {
        self.phi_ssa.get(phi_idx)
    }
}

#[derive(Default)]
struct PhiSrcMap {
    src_phi: HashMap<SSAValue, u32>,
}

impl PhiSrcMap {
    fn new() -> PhiSrcMap {
        Default::default()
    }

    fn add_phi_src(&mut self, phi_idx: u32, src: Src) {
        debug_assert!(src.is_unmodified());
        let vec = src.src_ref.as_ssa().expect("Not an SSA source");
        debug_assert!(vec.comps() == 1);
        self.src_phi.insert(vec[0], phi_idx);
    }

    pub fn from_block(block: &BasicBlock) -> PhiSrcMap {
        let mut map = PhiSrcMap::new();
        if let Some(phi) = block.phi_srcs() {
            for (idx, src) in phi.srcs.iter() {
                map.add_phi_src(*idx, *src);
            }
        }
        map
    }

    pub fn get_phi_idx(&self, ssa: &SSAValue) -> Option<&u32> {
        self.src_phi.get(ssa)
    }
}

trait Spill {
    fn spill_file(&self, file: RegFile) -> RegFile;
    fn spill(&mut self, dst: SSAValue, src: Src) -> Box<Instr>;
    fn fill(&mut self, dst: Dst, src: SSAValue) -> Box<Instr>;
}

struct SpillUniform<'a> {
    info: &'a mut ShaderInfo,
}

impl<'a> SpillUniform<'a> {
    fn new(info: &'a mut ShaderInfo) -> Self {
        Self { info }
    }
}

impl Spill for SpillUniform<'_> {
    fn spill_file(&self, file: RegFile) -> RegFile {
        debug_assert!(file.is_uniform());
        file.to_warp()
    }

    fn spill(&mut self, dst: SSAValue, src: Src) -> Box<Instr> {
        self.info.num_spills_to_reg += 1;
        Instr::new_boxed(OpCopy {
            dst: dst.into(),
            src: src,
        })
    }

    fn fill(&mut self, dst: Dst, src: SSAValue) -> Box<Instr> {
        self.info.num_fills_from_reg += 1;
        Instr::new_boxed(OpR2UR {
            dst: dst,
            src: src.into(),
        })
    }
}

struct SpillPred<'a> {
    info: &'a mut ShaderInfo,
}

impl<'a> SpillPred<'a> {
    fn new(info: &'a mut ShaderInfo) -> Self {
        Self { info }
    }
}

impl Spill for SpillPred<'_> {
    fn spill_file(&self, file: RegFile) -> RegFile {
        match file {
            RegFile::Pred => RegFile::GPR,
            RegFile::UPred => RegFile::UGPR,
            _ => panic!("Unsupported register file"),
        }
    }

    fn spill(&mut self, dst: SSAValue, src: Src) -> Box<Instr> {
        assert!(matches!(dst.file(), RegFile::GPR | RegFile::UGPR));
        self.info.num_spills_to_reg += 1;
        if let Some(b) = src.as_bool() {
            let u32_src = if b {
                Src::new_imm_u32(!0)
            } else {
                Src::new_zero()
            };
            Instr::new_boxed(OpCopy {
                dst: dst.into(),
                src: u32_src,
            })
        } else {
            Instr::new_boxed(OpSel {
                dst: dst.into(),
                cond: src.bnot(),
                srcs: [Src::new_zero(), Src::new_imm_u32(!0)],
            })
        }
    }

    fn fill(&mut self, dst: Dst, src: SSAValue) -> Box<Instr> {
        assert!(matches!(src.file(), RegFile::GPR | RegFile::UGPR));
        self.info.num_fills_from_reg += 1;
        Instr::new_boxed(OpISetP {
            dst: dst,
            set_op: PredSetOp::And,
            cmp_op: IntCmpOp::Ne,
            cmp_type: IntCmpType::U32,
            ex: false,
            srcs: [Src::new_zero(), src.into()],
            accum: true.into(),
            low_cmp: true.into(),
        })
    }
}

struct SpillBar<'a> {
    info: &'a mut ShaderInfo,
}

impl<'a> SpillBar<'a> {
    fn new(info: &'a mut ShaderInfo) -> Self {
        Self { info }
    }
}

impl Spill for SpillBar<'_> {
    fn spill_file(&self, file: RegFile) -> RegFile {
        assert!(file == RegFile::Bar);
        RegFile::GPR
    }

    fn spill(&mut self, dst: SSAValue, src: Src) -> Box<Instr> {
        assert!(dst.file() == RegFile::GPR);
        self.info.num_spills_to_reg += 1;
        Instr::new_boxed(OpBMov {
            dst: dst.into(),
            src: src,
            clear: false,
        })
    }

    fn fill(&mut self, dst: Dst, src: SSAValue) -> Box<Instr> {
        assert!(src.file() == RegFile::GPR);
        self.info.num_fills_from_reg += 1;
        Instr::new_boxed(OpBMov {
            dst: dst,
            src: src.into(),
            clear: false,
        })
    }
}

struct SpillGPR<'a> {
    info: &'a mut ShaderInfo,
}

impl<'a> SpillGPR<'a> {
    fn new(info: &'a mut ShaderInfo) -> Self {
        Self { info }
    }
}

impl Spill for SpillGPR<'_> {
    fn spill_file(&self, file: RegFile) -> RegFile {
        assert!(file == RegFile::GPR);
        RegFile::Mem
    }

    fn spill(&mut self, dst: SSAValue, src: Src) -> Box<Instr> {
        assert!(dst.file() == RegFile::Mem);
        self.info.num_spills_to_mem += 1;
        if let Some(ssa) = src.as_ssa() {
            assert!(ssa.file() == Some(RegFile::GPR));
            Instr::new_boxed(OpCopy {
                dst: dst.into(),
                src: src,
            })
        } else {
            // We use parallel copies for spilling non-GPR things to Mem
            let mut pcopy = OpParCopy::new();
            pcopy.push(dst.into(), src);
            Instr::new_boxed(pcopy)
        }
    }

    fn fill(&mut self, dst: Dst, src: SSAValue) -> Box<Instr> {
        assert!(src.file() == RegFile::Mem);
        self.info.num_fills_from_mem += 1;
        Instr::new_boxed(OpCopy {
            dst: dst,
            src: src.into(),
        })
    }
}

#[derive(Eq, PartialEq)]
struct SSANextUse {
    ssa: SSAValue,
    next_use: usize,
}

impl SSANextUse {
    fn new(ssa: SSAValue, next_use: usize) -> SSANextUse {
        SSANextUse {
            ssa: ssa,
            next_use: next_use,
        }
    }
}

impl Ord for SSANextUse {
    fn cmp(&self, other: &Self) -> Ordering {
        self.next_use
            .cmp(&other.next_use)
            .then_with(|| self.ssa.idx().cmp(&other.ssa.idx()))
    }
}

impl PartialOrd for SSANextUse {
    fn partial_cmp(&self, other: &Self) -> Option<Ordering> {
        Some(self.cmp(other))
    }
}

struct SpillCache<'a, S: Spill> {
    alloc: &'a mut SSAValueAllocator,
    spill: S,
    const_tracker: ConstTracker,
    val_spill: HashMap<SSAValue, SSAValue>,
}

impl<'a, S: Spill> SpillCache<'a, S> {
    fn new(alloc: &'a mut SSAValueAllocator, spill: S) -> SpillCache<'a, S> {
        SpillCache {
            alloc: alloc,
            spill: spill,
            const_tracker: ConstTracker::new(),
            val_spill: HashMap::new(),
        }
    }

    fn add_copy_if_const(&mut self, op: &OpCopy) {
        self.const_tracker.add_copy(op);
    }

    fn is_const(&self, ssa: &SSAValue) -> bool {
        self.const_tracker.contains(ssa)
    }

    fn spill_file(&self, file: RegFile) -> RegFile {
        self.spill.spill_file(file)
    }

    fn get_spill(&mut self, ssa: SSAValue) -> SSAValue {
        *self.val_spill.entry(ssa).or_insert_with(|| {
            self.alloc.alloc(self.spill.spill_file(ssa.file()))
        })
    }

    fn spill_src(&mut self, ssa: SSAValue, src: Src) -> Box<Instr> {
        let dst = self.get_spill(ssa);
        self.spill.spill(dst, src)
    }

    fn spill(&mut self, ssa: SSAValue) -> Box<Instr> {
        if let Some(c) = self.const_tracker.get(&ssa) {
            self.spill_src(ssa, (*c).into())
        } else {
            self.spill_src(ssa, ssa.into())
        }
    }

    fn fill_dst(&mut self, dst: Dst, ssa: SSAValue) -> Box<Instr> {
        let src = self.get_spill(ssa);
        self.spill.fill(dst, src)
    }

    fn fill(&mut self, ssa: SSAValue) -> Box<Instr> {
        if let Some(c) = self.const_tracker.get(&ssa) {
            Instr::new_boxed(OpCopy {
                dst: ssa.into(),
                src: (*c).into(),
            })
        } else {
            self.fill_dst(ssa.into(), ssa)
        }
    }
}

struct SpillChooser<'a> {
    bl: &'a NextUseBlockLiveness,
    pinned: &'a HashSet<SSAValue>,
    ip: usize,
    count: usize,
    spills: BinaryHeap<Reverse<SSANextUse>>,
    min_next_use: usize,
}

struct SpillChoiceIter {
    spills: BinaryHeap<Reverse<SSANextUse>>,
}

impl<'a> SpillChooser<'a> {
    pub fn new(
        bl: &'a NextUseBlockLiveness,
        pinned: &'a HashSet<SSAValue>,
        ip: usize,
        count: usize,
    ) -> Self {
        Self {
            bl: bl,
            pinned: pinned,
            ip: ip,
            count: count,
            spills: BinaryHeap::new(),
            min_next_use: ip + 1,
        }
    }

    pub fn add_candidate(&mut self, ssa: SSAValue) {
        // Don't spill anything that's pinned
        if self.pinned.contains(&ssa) {
            return;
        }

        // Ignore anything used sonner than spill options we've already
        // rejected.
        let next_use = self.bl.next_use_after_or_at_ip(&ssa, self.ip).unwrap();
        if next_use < self.min_next_use {
            return;
        }

        self.spills.push(Reverse(SSANextUse::new(ssa, next_use)));

        if self.spills.len() > self.count {
            // Because we reversed the heap, pop actually removes the
            // one with the lowest next_use which is what we want here.
            let old = self.spills.pop().unwrap();
            debug_assert!(self.spills.len() == self.count);
            self.min_next_use = max(self.min_next_use, old.0.next_use);
        }
    }
}

impl<'a> IntoIterator for SpillChooser<'a> {
    type Item = SSAValue;
    type IntoIter = SpillChoiceIter;

    fn into_iter(self) -> SpillChoiceIter {
        SpillChoiceIter {
            spills: self.spills,
        }
    }
}

impl Iterator for SpillChoiceIter {
    type Item = SSAValue;

    fn size_hint(&self) -> (usize, Option<usize>) {
        let len = self.spills.len();
        (len, Some(len))
    }

    fn next(&mut self) -> Option<SSAValue> {
        self.spills.pop().map(|x| x.0.ssa)
    }
}

#[derive(Clone)]
struct SSAState {
    // The set of variables which currently exist in registers
    w: LiveSet,
    // The set of variables which have already been spilled.  These don't need
    // to be spilled again.
    s: HashSet<SSAValue>,
    // The set of pinned variables
    p: HashSet<SSAValue>,
}

fn spill_values<S: Spill>(
    func: &mut Function,
    file: RegFile,
    limit: u32,
    spill: S,
) {
    let files = RegFileSet::from_iter([file]);
    let live = NextUseLiveness::for_function(func, &files);
    let blocks = &mut func.blocks;

    // Record the set of SSA values used within each loop
    let mut phi_dst_maps = Vec::new();
    let mut phi_src_maps = Vec::new();
    let mut loop_uses = HashMap::new();
    for b_idx in 0..blocks.len() {
        phi_dst_maps.push(PhiDstMap::from_block(&blocks[b_idx]));
        phi_src_maps.push(PhiSrcMap::from_block(&blocks[b_idx]));

        if let Some(lh_idx) = blocks.loop_header_index(b_idx) {
            let uses = loop_uses
                .entry(lh_idx)
                .or_insert_with(|| RefCell::new(HashSet::new()));
            let uses = uses.get_mut();

            for instr in &blocks[b_idx].instrs {
                instr.for_each_ssa_use(|ssa| {
                    if ssa.file() == file {
                        uses.insert(*ssa);
                    }
                });
            }
        }
    }

    if !loop_uses.is_empty() {
        // The previous loop only added values to the uses set for the
        // inner-most loop.  Propagate from inner loops to outer loops.
        for b_idx in (0..blocks.len()).rev() {
            let Some(uses) = loop_uses.get(&b_idx) else {
                continue;
            };
            let uses = uses.borrow();

            let Some(dom) = blocks.dom_parent_index(b_idx) else {
                continue;
            };

            let Some(dom_lh_idx) = blocks.loop_header_index(dom) else {
                continue;
            };

            let mut parent_uses =
                loop_uses.get(&dom_lh_idx).unwrap().borrow_mut();
            for ssa in uses.iter() {
                parent_uses.insert(*ssa);
            }
        }
    }

    let mut spill = SpillCache::new(&mut func.ssa_alloc, spill);
    let mut spilled_phis = BitSet::new();

    let mut ssa_state_in: Vec<SSAState> = Vec::new();
    let mut ssa_state_out: Vec<SSAState> = Vec::new();

    for b_idx in 0..blocks.len() {
        let bl = live.block_live(b_idx);

        let preds = blocks.pred_indices(b_idx).to_vec();
        let w = if preds.is_empty() {
            // This is the start block so we start with nothing in
            // registers.
            LiveSet::new()
        } else if preds.len() == 1 {
            // If we only have one predecessor then it can't possibly be a
            // loop header and we can just copy the predecessor's w.
            assert!(!blocks.is_loop_header(b_idx));
            assert!(preds[0] < b_idx);
            let p_w = &ssa_state_out[preds[0]].w;
            LiveSet::from_iter(
                p_w.iter().filter(|ssa| bl.is_live_in(ssa)).cloned(),
            )
        } else if !blocks[b_idx].uniform && file.is_uniform() {
            // If this is a non-uniform block, then we can't spill or fill any
            // uniform registers.  The good news is that none of our non-uniform
            // predecessors could spill, either, so we know that everything that
            // was resident coming in will fit in the register file.
            let mut w = LiveSet::new();
            for p_idx in &preds {
                if *p_idx < b_idx {
                    let p_w = &ssa_state_out[*p_idx].w;
                    w.extend(
                        p_w.iter().filter(|ssa| bl.is_live_in(ssa)).cloned(),
                    );
                }
            }
            debug_assert!(w.count(file) <= limit);
            w
        } else if blocks.is_loop_header(b_idx) {
            let mut i_b: HashSet<SSAValue> =
                HashSet::from_iter(bl.iter_live_in().cloned());

            if let Some(phi) = blocks[b_idx].phi_dsts() {
                for (_, dst) in phi.dsts.iter() {
                    if let Dst::SSA(vec) = dst {
                        assert!(vec.comps() == 1);
                        let ssa = vec[0];
                        if ssa.file() == file {
                            i_b.insert(ssa);
                        }
                    }
                }
            }

            let lu = loop_uses.get(&b_idx).unwrap().borrow();
            let mut w = LiveSet::new();

            let mut some = BinaryHeap::new();
            for ssa in i_b.iter() {
                if lu.contains(ssa) {
                    let next_use = bl.first_use(ssa).unwrap();
                    some.push(Reverse(SSANextUse::new(*ssa, next_use)));
                }
            }
            while w.count(file) < limit {
                let Some(entry) = some.pop() else {
                    break;
                };
                w.insert(entry.0.ssa);
            }

            // If we still have room, consider values which aren't used
            // inside the loop.
            if w.count(file) < limit {
                for ssa in i_b.iter() {
                    debug_assert!(ssa.file() == file);
                    if !lu.contains(ssa) {
                        let next_use = bl.first_use(ssa).unwrap();
                        some.push(Reverse(SSANextUse::new(*ssa, next_use)));
                    }
                }

                while w.count(file) < limit {
                    let Some(entry) = some.pop() else {
                        break;
                    };
                    w.insert(entry.0.ssa);
                }
            }

            w
        } else {
            let phi_dst_map = &phi_dst_maps[b_idx];

            struct SSAPredInfo {
                num_preds: usize,
                next_use: usize,
            }
            let mut live: HashMap<SSAValue, SSAPredInfo> = HashMap::new();

            for p_idx in &preds {
                let phi_src_map = &phi_src_maps[*p_idx];

                for mut ssa in ssa_state_out[*p_idx].w.iter().cloned() {
                    if let Some(phi) = phi_src_map.get_phi_idx(&ssa) {
                        ssa = *phi_dst_map.get_dst_ssa(phi).unwrap();
                    }

                    if let Some(next_use) = bl.first_use(&ssa) {
                        live.entry(ssa)
                            .and_modify(|e| e.num_preds += 1)
                            .or_insert_with(|| SSAPredInfo {
                                num_preds: 1,
                                next_use: next_use,
                            });
                    }
                }
            }

            let mut w = LiveSet::new();
            let mut some = BinaryHeap::new();

            for (ssa, info) in live.drain() {
                if info.num_preds == preds.len() {
                    // This one is in all the input sets
                    w.insert(ssa);
                } else {
                    some.push(Reverse(SSANextUse::new(ssa, info.next_use)));
                }
            }
            while w.count(file) < limit {
                let Some(entry) = some.pop() else {
                    break;
                };
                let ssa = entry.0.ssa;
                assert!(ssa.file() == file);
                w.insert(ssa);
            }

            w
        };

        let s = if preds.is_empty() {
            HashSet::new()
        } else if preds.len() == 1 {
            let p_s = &ssa_state_out[preds[0]].s;
            HashSet::from_iter(
                p_s.iter().filter(|ssa| bl.is_live_in(ssa)).cloned(),
            )
        } else {
            let mut s = HashSet::new();
            for p_idx in &preds {
                if *p_idx >= b_idx {
                    continue;
                }

                // We diverge a bit from Braun and Hack here.  They assume that
                // S is is a subset of W which is clearly bogus.  Instead, we
                // take the union of all forward edge predecessor S_out and
                // intersect with live-in for the current block.
                for ssa in ssa_state_out[*p_idx].s.iter() {
                    if bl.is_live_in(ssa) {
                        s.insert(*ssa);
                    }
                }
            }

            // The loop header heuristic sometimes drops stuff from W that has
            // never been spilled so we need to make sure everything live-in
            // which isn't in W is included in the spill set so that it gets
            // properly spilled when we spill across CF edges.
            if blocks.is_loop_header(b_idx) {
                for ssa in bl.iter_live_in() {
                    if !w.contains(ssa) {
                        s.insert(*ssa);
                    }
                }
            }

            s
        };

        let mut p = HashSet::new();
        for p_idx in &preds {
            if *p_idx < b_idx {
                let p_p = &ssa_state_out[*p_idx].p;
                p.extend(p_p.iter().filter(|ssa| bl.is_live_in(ssa)).cloned());
            }
        }

        for ssa in bl.iter_live_in() {
            debug_assert!(
                w.contains(ssa) || s.contains(ssa) || spill.is_const(ssa)
            );
        }

        let mut b = SSAState { w: w, s: s, p: p };

        assert!(ssa_state_in.len() == b_idx);
        ssa_state_in.push(b.clone());

        let bb = &mut blocks[b_idx];

        let mut instrs = Vec::new();
        for (ip, mut instr) in bb.instrs.drain(..).enumerate() {
            if let Op::Copy(op) = &instr.op {
                spill.add_copy_if_const(op);
            }

            match &mut instr.op {
                Op::PhiDsts(phi) => {
                    // For phis, anything that is not in W needs to be spilled
                    // by setting the destination to some spill value.
                    for (idx, dst) in phi.dsts.iter_mut() {
                        let vec = dst.as_ssa().unwrap();
                        debug_assert!(vec.comps() == 1);
                        let ssa = &vec[0];

                        if ssa.file() == file && !b.w.contains(ssa) {
                            spilled_phis.insert((*idx).try_into().unwrap());
                            b.s.insert(*ssa);
                            *dst = spill.get_spill(*ssa).into();
                        }
                    }
                }
                Op::PhiSrcs(_) => {
                    // We handle phi sources later.  For now, leave them be.
                }
                Op::ParCopy(pcopy) => {
                    let mut num_w_dsts = 0_u32;
                    for (dst, src) in pcopy.dsts_srcs.iter_mut() {
                        let dst_vec = dst.as_ssa().unwrap();
                        debug_assert!(dst_vec.comps() == 1);
                        let dst_ssa = &dst_vec[0];

                        debug_assert!(src.is_unmodified());
                        let Some(src_vec) = src.src_ref.as_ssa() else {
                            continue;
                        };
                        debug_assert!(src_vec.comps() == 1);
                        let src_ssa = &src_vec[0];

                        debug_assert!(dst_ssa.file() == src_ssa.file());
                        if src_ssa.file() != file {
                            continue;
                        }

                        // If it's not resident, rewrite to just move from one
                        // spill to another, assuming that copying in spill
                        // space is efficient
                        if b.w.contains(src_ssa) {
                            num_w_dsts += 1;
                        } else {
                            if b.s.insert(*src_ssa) {
                                assert!(spill.is_const(src_ssa));
                                instrs.push(spill.spill(*src_ssa));
                            }
                            b.s.insert(*dst_ssa);
                            *src = spill.get_spill(*src_ssa).into();
                            *dst = spill.get_spill(*dst_ssa).into();
                        }
                    }

                    // We can now assume that a source is in W if and only if
                    // the file matches.  Remove all killed sources from W.
                    for (_, src) in pcopy.dsts_srcs.iter() {
                        let Some(src_vec) = src.src_ref.as_ssa() else {
                            continue;
                        };
                        let src_ssa = &src_vec[0];
                        if !bl.is_live_after_ip(src_ssa, ip) {
                            b.w.remove(src_ssa);
                        }
                    }

                    let rel_limit = limit - b.w.count(file);
                    if num_w_dsts > rel_limit {
                        // We can't spill uniform registers in a non-uniform
                        // block
                        assert!(bb.uniform || !file.is_uniform());

                        let count = num_w_dsts - rel_limit;
                        let count = count.try_into().unwrap();

                        let mut spills = SpillChooser::new(bl, &b.p, ip, count);
                        for (dst, _) in pcopy.dsts_srcs.iter() {
                            let dst_ssa = &dst.as_ssa().unwrap()[0];
                            if dst_ssa.file() == file {
                                spills.add_candidate(*dst_ssa);
                            }
                        }

                        let spills: HashSet<SSAValue> =
                            HashSet::from_iter(spills);

                        for (dst, src) in pcopy.dsts_srcs.iter_mut() {
                            let dst_ssa = &dst.as_ssa().unwrap()[0];
                            let src_ssa = &src.src_ref.as_ssa().unwrap()[0];
                            if spills.contains(dst_ssa) {
                                if b.s.insert(*src_ssa) {
                                    if DEBUG.annotate() {
                                        instrs.push(Instr::new_boxed(
                                            OpAnnotate {
                                                annotation:
                                                    "generated by spill_values"
                                                        .into(),
                                            },
                                        ));
                                    }
                                    instrs.push(spill.spill(*src_ssa));
                                }
                                b.s.insert(*dst_ssa);
                                *src = spill.get_spill(*src_ssa).into();
                                *dst = spill.get_spill(*dst_ssa).into();
                            }
                        }
                    }

                    for (dst, _) in pcopy.dsts_srcs.iter() {
                        let dst_ssa = &dst.as_ssa().unwrap()[0];
                        if dst_ssa.file() == file {
                            b.w.insert(*dst_ssa);
                        }
                    }
                }
                _ => {
                    if file == RegFile::UGPR && !bb.uniform {
                        // We can't spill UGPRs in a non-uniform block.
                        // Instead, we depend on two facts:
                        //
                        //  1. Uniform instructions are not allowed in
                        //     non-uniform blocks
                        //
                        //  2. Non-uniform instructions can always accept a wave
                        //     register in leu of a uniform register
                        //
                        debug_assert!(spill.spill_file(file) == RegFile::GPR);
                        instr.for_each_ssa_use_mut(|ssa| {
                            if ssa.file() == file && !b.w.contains(ssa) {
                                if b.s.insert(*ssa) {
                                    assert!(spill.is_const(ssa));
                                    instrs.push(spill.spill(*ssa));
                                }
                                *ssa = spill.get_spill(*ssa).into();
                            }
                        });
                    } else if file == RegFile::UPred && !bb.uniform {
                        // We can't spill UPreds in a non-uniform block.
                        // Instead, we depend on two facts:
                        //
                        //  1. Uniform instructions are not allowed in
                        //     non-uniform blocks
                        //
                        //  2. Non-uniform instructions can always accept a wave
                        //     register in leu of a uniform register
                        //
                        //  3. We can un-spill from a UGPR directly to a Pred
                        //
                        // This also shouldn't come up that often in practice
                        // so it's okay to un-spill every time on the spot.
                        //
                        instr.for_each_ssa_use_mut(|ssa| {
                            if ssa.file() == file && !b.w.contains(ssa) {
                                if DEBUG.annotate() {
                                    instrs.push(Instr::new_boxed(OpAnnotate {
                                        annotation: "generated by spill_values"
                                            .into(),
                                    }));
                                }
                                let tmp = spill.alloc.alloc(RegFile::Pred);
                                instrs.push(spill.fill_dst(tmp.into(), *ssa));
                                *ssa = tmp;
                            }
                        });
                    } else {
                        // First compute fills even though those have to come
                        // after spills.
                        let mut fills = Vec::new();
                        instr.for_each_ssa_use(|ssa| {
                            if ssa.file() == file && !b.w.contains(ssa) {
                                debug_assert!(
                                    b.s.contains(ssa) || spill.is_const(ssa)
                                );
                                debug_assert!(bb.uniform || !ssa.is_uniform());
                                fills.push(spill.fill(*ssa));
                                b.w.insert(*ssa);
                            }
                        });

                        let rel_pressure =
                            bl.get_instr_pressure(ip, &instr)[file];
                        let abs_pressure =
                            b.w.count(file) + u32::from(rel_pressure);

                        if abs_pressure > limit {
                            let count = abs_pressure - limit;
                            let count = count.try_into().unwrap();

                            let mut spills =
                                SpillChooser::new(bl, &b.p, ip, count);
                            for ssa in b.w.iter() {
                                spills.add_candidate(*ssa);
                            }

                            for ssa in spills {
                                debug_assert!(ssa.file() == file);
                                b.w.remove(&ssa);
                                if !spill.is_const(&ssa) {
                                    if DEBUG.annotate() {
                                        instrs.push(Instr::new_boxed(
                                            OpAnnotate {
                                                annotation:
                                                    "generated by spill_values"
                                                        .into(),
                                            },
                                        ));
                                    }
                                    instrs.push(spill.spill(ssa));
                                    b.s.insert(ssa);
                                }
                            }
                        }

                        if DEBUG.annotate() {
                            instrs.push(Instr::new_boxed(OpAnnotate {
                                annotation: "generated by spill_values".into(),
                            }));
                        }
                        instrs.append(&mut fills);

                        instr.for_each_ssa_use(|ssa| {
                            if ssa.file() == file {
                                debug_assert!(b.w.contains(ssa));
                            }
                        });

                        b.w.insert_instr_top_down(ip, &instr, bl);
                    }
                }
            }

            // OpPin takes the normal spilling path but we want to also mark any
            // of its destination SSA values as pinned.
            if matches!(&instr.op, Op::Pin(_)) {
                instr.for_each_ssa_def(|ssa| {
                    b.p.insert(*ssa);
                });
            }

            instrs.push(instr);
        }
        bb.instrs = instrs;

        assert!(ssa_state_out.len() == b_idx);
        ssa_state_out.push(b);
    }

    // Now that everthing is spilled, we handle phi sources and connect the
    // blocks by adding spills and fills as needed along edges.
    for p_idx in 0..blocks.len() {
        let succ = blocks.succ_indices(p_idx);
        if succ.len() != 1 {
            // We don't have any critical edges
            for s_idx in succ {
                debug_assert!(blocks.pred_indices(*s_idx).len() == 1);
            }
            continue;
        }
        let s_idx = succ[0];

        let pb = &mut blocks[p_idx];
        let p_out = &ssa_state_out[p_idx];
        let s_in = &ssa_state_in[s_idx];
        let phi_dst_map = &phi_dst_maps[s_idx];

        let mut spills = Vec::new();
        let mut fills = Vec::new();

        if let Some(phi) = pb.phi_srcs_mut() {
            for (idx, src) in phi.srcs.iter_mut() {
                debug_assert!(src.is_unmodified());
                let vec = src.src_ref.as_ssa().unwrap();
                debug_assert!(vec.comps() == 1);
                let ssa = &vec[0];

                if ssa.file() != file {
                    continue;
                }

                if spilled_phis.get((*idx).try_into().unwrap()) {
                    if !p_out.s.contains(ssa) {
                        spills.push(*ssa);
                    }
                    *src = spill.get_spill(*ssa).into();
                } else {
                    if !p_out.w.contains(ssa) {
                        fills.push(*ssa);
                    }
                }
            }
        }

        for ssa in s_in.s.iter() {
            if !p_out.s.contains(ssa) {
                assert!(p_out.w.contains(ssa) || spill.is_const(ssa));
                spills.push(*ssa);
            }
        }

        for ssa in s_in.w.iter() {
            if phi_dst_map.get_phi_idx(ssa).is_some() {
                continue;
            }

            if !p_out.w.contains(ssa) {
                fills.push(*ssa);
            }
        }

        if spills.is_empty() && fills.is_empty() {
            continue;
        }

        // Sort to ensure stability of the algorithm
        spills.sort_by_key(|ssa| ssa.idx());
        fills.sort_by_key(|ssa| ssa.idx());

        let mut instrs = Vec::new();
        for ssa in spills {
            instrs.push(spill.spill(ssa));
        }
        for ssa in fills {
            debug_assert!(pb.uniform || !ssa.is_uniform());
            instrs.push(spill.fill(ssa));
        }

        // Insert spills and fills right after the phi (if any)
        let ip = pb
            .phi_srcs_ip()
            .or_else(|| pb.branch_ip())
            .unwrap_or_else(|| pb.instrs.len());
        pb.instrs.splice(ip..ip, instrs.into_iter());
    }
}

impl Function {
    /// Spill values from @file to fit within @limit registers
    ///
    /// This pass assumes that the function is already in CSSA form.  See
    /// @to_cssa for more details.
    ///
    /// The algorithm implemented here is roughly based on "Register Spilling
    /// and Live-Range Splitting for SSA-Form Programs" by Braun and Hack.  The
    /// primary contributions of the Braun and Hack paper are the global
    /// next-use distances which are implemented by @NextUseLiveness and a
    /// heuristic for computing spill sets at block boundaries.  The paper
    /// describes two sets:
    ///
    ///  - W, the set of variables currently resident
    ///
    ///  - S, the set of variables which have been spilled
    ///
    /// These sets are tracked as we walk instructions and [un]spill values to
    /// satisfy the given limit.  When spills are required we spill the value
    /// with the nighest next-use IP.  At block boundaries, Braun and Hack
    /// describe a heuristic for determining the starting W and S sets based on
    /// the W and S from the end of each of the forward edge predecessor blocks.
    ///
    /// What Braun and Hack do not describe is how to handle phis and parallel
    /// copies.  Because we assume the function is already in CSSA form, we can
    /// use a fairly simple algorithm.  On the first pass, we ignore phi sources
    /// and assign phi destinations based on W at the start of the block.  If
    /// the phi destination is in W, we leave it alone.  If it is not in W, then
    /// we allocate a new spill value and assign it to the phi destination.  In
    /// a second pass, we handle phi sources based on the destination.  If the
    /// destination is in W, we leave it alone.  If the destination is spilled,
    /// we read from the spill value corresponding to the source, spilling first
    /// if needed.  In the second pass, we also handle spilling across blocks as
    /// needed for values that do not pass through a phi.
    ///
    /// A special case is also required for parallel copies because they can
    /// have an unbounded number of destinations.  For any source values not in
    /// W, we allocate a spill value for the destination and copy in the spill
    /// register file.  For any sources which are in W, we try to leave as much
    /// in W as possible.  However, since source values may not be killed by the
    /// copy and because one source value may be copied to arbitrarily many
    /// destinations, that is not always possible.  Whenever we need to spill
    /// values, we spill according to the highest next-use of the destination
    /// and we spill the source first and then parallel copy the source into a
    /// spilled destination value.
    ///
    /// This all assumes that it's better to copy in spill space than to unspill
    /// just for the sake of a parallel copy.  While this may not be true in
    /// general, especially not when spilling to memory, the register allocator
    /// is good at eliding unnecessary copies.
    pub fn spill_values(
        &mut self,
        file: RegFile,
        limit: u32,
        info: &mut ShaderInfo,
    ) {
        match file {
            RegFile::GPR => {
                let spill = SpillGPR::new(info);
                spill_values(self, file, limit, spill);
            }
            RegFile::UGPR => {
                let spill = SpillUniform::new(info);
                spill_values(self, file, limit, spill);
            }
            RegFile::Pred => {
                let spill = SpillPred::new(info);
                spill_values(self, file, limit, spill);
            }
            RegFile::UPred => {
                let spill = SpillPred::new(info);
                spill_values(self, file, limit, spill);
            }
            RegFile::Bar => {
                let spill = SpillBar::new(info);
                spill_values(self, file, limit, spill);
            }
            _ => panic!("Don't know how to spill {} registers", file),
        }

        self.repair_ssa();
        self.opt_dce();

        if DEBUG.print() {
            eprintln!("NAK IR after spilling {}:\n{}", file, self);
        }
    }
}
