// Copyright © 2022 Collabora, Ltd.
// SPDX-License-Identifier: MIT

use crate::ir::*;

use std::collections::HashMap;

enum CBufRule {
    Yes,
    No,
    BindlessRequiresBlock(usize),
}

impl CBufRule {
    fn allows_src(&self, src_bi: usize, src: &Src) -> bool {
        let SrcRef::CBuf(cb) = &src.src_ref else {
            return true;
        };

        match self {
            CBufRule::Yes => true,
            CBufRule::No => false,
            CBufRule::BindlessRequiresBlock(bi) => match cb.buf {
                CBuf::Binding(_) => true,
                CBuf::BindlessSSA(_) => src_bi == *bi,
                CBuf::BindlessUGPR(_) => panic!("Not in SSA form"),
            },
        }
    }
}

struct CopyEntry {
    bi: usize,
    src_type: SrcType,
    src: Src,
}

struct PrmtEntry {
    bi: usize,
    sel: PrmtSel,
    srcs: [Src; 2],
}

/// This entry tracks b2i conversions
struct ConvBoolToInt {
    src: Src,
}

/// This entry tracks i2b conversions
struct ConvIntToBool {
    src: Src,
    inverted: bool,
}

enum CopyPropEntry {
    Copy(CopyEntry),
    Prmt(PrmtEntry),
    ConvBoolToInt(ConvBoolToInt),
    ConvIntToBool(ConvIntToBool),
}

struct CopyPropPass {
    ssa_map: HashMap<SSAValue, CopyPropEntry>,
}

impl CopyPropPass {
    pub fn new() -> CopyPropPass {
        CopyPropPass {
            ssa_map: HashMap::new(),
        }
    }

    fn add_copy(
        &mut self,
        bi: usize,
        dst: SSAValue,
        src_type: SrcType,
        src: Src,
    ) {
        assert!(src.src_ref.get_reg().is_none());
        self.ssa_map
            .insert(dst, CopyPropEntry::Copy(CopyEntry { bi, src_type, src }));
    }

    fn add_b2i(&mut self, dst: SSAValue, src: Src) {
        assert!(src.src_ref.get_reg().is_none());
        assert!(dst.is_gpr());
        self.ssa_map
            .insert(dst, CopyPropEntry::ConvBoolToInt(ConvBoolToInt { src }));
    }

    fn add_i2b(&mut self, dst: SSAValue, src: Src, inverted: bool) {
        assert!(src.src_ref.get_reg().is_none());
        assert!(dst.is_predicate());
        self.ssa_map.insert(
            dst,
            CopyPropEntry::ConvIntToBool(ConvIntToBool { src, inverted }),
        );
    }

    fn add_prmt(
        &mut self,
        bi: usize,
        dst: SSAValue,
        sel: PrmtSel,
        srcs: [Src; 2],
    ) {
        assert!(
            srcs[0].src_ref.get_reg().is_none()
                && srcs[1].src_ref.get_reg().is_none()
        );
        self.ssa_map
            .insert(dst, CopyPropEntry::Prmt(PrmtEntry { bi, sel, srcs }));
    }

    fn add_fp64_copy(&mut self, bi: usize, dst: &SSARef, src: Src) {
        assert!(dst.comps() == 2);
        match src.src_ref {
            SrcRef::Zero | SrcRef::Imm32(_) => {
                self.add_copy(bi, dst[0], SrcType::ALU, Src::new_zero());
                self.add_copy(bi, dst[1], SrcType::F64, src);
            }
            SrcRef::CBuf(cb) => {
                let lo32 = Src::from(SrcRef::CBuf(cb));
                let hi32 = Src {
                    src_ref: SrcRef::CBuf(cb.offset(4)),
                    src_mod: src.src_mod,
                    src_swizzle: src.src_swizzle,
                };
                self.add_copy(bi, dst[0], SrcType::ALU, lo32);
                self.add_copy(bi, dst[1], SrcType::F64, hi32);
            }
            SrcRef::SSA(ssa) => {
                assert!(ssa.comps() == 2);
                let lo32 = Src::from(ssa[0]);
                let hi32 = Src {
                    src_ref: ssa[1].into(),
                    src_mod: src.src_mod,
                    src_swizzle: src.src_swizzle,
                };
                self.add_copy(bi, dst[0], SrcType::ALU, lo32);
                self.add_copy(bi, dst[1], SrcType::F64, hi32);
            }
            _ => (),
        }
    }

    fn get_copy(&self, dst: &SSAValue) -> Option<&CopyPropEntry> {
        self.ssa_map.get(dst)
    }

    fn prop_to_pred(&self, pred: &mut Pred) {
        loop {
            let src_ssa = match &pred.pred_ref {
                PredRef::SSA(ssa) => ssa,
                _ => return,
            };

            let Some(CopyPropEntry::Copy(entry)) = self.get_copy(src_ssa)
            else {
                return;
            };

            match entry.src.src_ref {
                SrcRef::True => {
                    pred.pred_ref = PredRef::None;
                }
                SrcRef::False => {
                    pred.pred_ref = PredRef::None;
                    pred.pred_inv = !pred.pred_inv;
                }
                SrcRef::SSA(ssa) => {
                    assert!(ssa.comps() == 1);
                    pred.pred_ref = PredRef::SSA(ssa[0]);
                }
                _ => return,
            }

            match entry.src.src_mod {
                SrcMod::None => (),
                SrcMod::BNot => {
                    pred.pred_inv = !pred.pred_inv;
                }
                _ => panic!("Invalid predicate modifier"),
            }
        }
    }

    fn prop_to_ssa_ref(&self, src_ssa: &mut SSARef) -> bool {
        let mut progress = false;

        for c in 0..src_ssa.comps() {
            let c_ssa = &mut src_ssa[usize::from(c)];
            let Some(CopyPropEntry::Copy(entry)) = self.get_copy(c_ssa) else {
                continue;
            };

            if entry.src.is_unmodified() {
                if let SrcRef::SSA(entry_ssa) = entry.src.src_ref {
                    assert!(entry_ssa.comps() == 1);
                    *c_ssa = entry_ssa[0];
                    progress = true;
                }
            }
        }

        progress
    }

    fn prop_to_ssa_src(&self, src: &mut Src) {
        assert!(src.is_unmodified());
        if let SrcRef::SSA(src_ssa) = &mut src.src_ref {
            loop {
                if !self.prop_to_ssa_ref(src_ssa) {
                    break;
                }
            }
        }
    }

    fn prop_to_gpr_src(&self, src: &mut Src) {
        loop {
            let src_ssa = match &mut src.src_ref {
                SrcRef::SSA(ssa) => {
                    // First, try to propagate SSA components
                    if self.prop_to_ssa_ref(ssa) {
                        continue;
                    }
                    ssa
                }
                _ => return,
            };

            for c in 0..usize::from(src_ssa.comps()) {
                let Some(CopyPropEntry::Copy(entry)) =
                    self.get_copy(&src_ssa[c])
                else {
                    return;
                };

                match entry.src.src_ref {
                    SrcRef::Zero | SrcRef::Imm32(0) => (),
                    _ => return,
                }
            }

            // If we got here, all the components are zero
            src.src_ref = SrcRef::Zero;
        }
    }

    fn prop_to_scalar_src(
        &self,
        src_type: SrcType,
        cbuf_rule: &CBufRule,
        src: &mut Src,
    ) {
        loop {
            let src_ssa = match &src.src_ref {
                SrcRef::SSA(ssa) => ssa,
                _ => return,
            };

            assert!(src_ssa.comps() == 1);
            let entry = match self.get_copy(&src_ssa[0]) {
                Some(e) => e,
                None => return,
            };

            match entry {
                CopyPropEntry::Copy(entry) => {
                    if !cbuf_rule.allows_src(entry.bi, &entry.src) {
                        return;
                    }

                    // If there are modifiers, the source types have to match
                    if !entry.src.is_unmodified() && entry.src_type != src_type
                    {
                        return;
                    }

                    src.src_ref = entry.src.src_ref;
                    src.src_mod = entry.src.src_mod.modify(src.src_mod);
                }
                CopyPropEntry::Prmt(entry) => {
                    // Turn the swizzle into a permute. For F16, we use Xx to
                    // indicate that it only takes the bottom 16 bits.
                    let swizzle_prmt: [u8; 4] = match src_type {
                        SrcType::F16 => [0, 1, 0, 1],
                        SrcType::F16v2 => match src.src_swizzle {
                            SrcSwizzle::None => [0, 1, 2, 3],
                            SrcSwizzle::Xx => [0, 1, 0, 1],
                            SrcSwizzle::Yy => [2, 3, 2, 3],
                        },
                        _ => [0, 1, 2, 3],
                    };

                    let mut entry_src_idx = None;
                    let mut combined = [0_u8; 4];

                    for i in 0..4 {
                        let prmt_byte = entry.sel.get(swizzle_prmt[i].into());

                        // If we have a sign extension, we cannot simplify it.
                        if prmt_byte.msb() {
                            return;
                        }

                        // Ensure we are using the same source, we cannot
                        // combine multiple sources.
                        if entry_src_idx.is_none() {
                            entry_src_idx = Some(prmt_byte.src());
                        } else if entry_src_idx != Some(prmt_byte.src()) {
                            return;
                        }

                        combined[i] = prmt_byte.byte().try_into().unwrap();
                    }

                    let entry_src_idx = usize::from(entry_src_idx.unwrap());
                    let entry_src = entry.srcs[entry_src_idx];

                    if !cbuf_rule.allows_src(entry.bi, &entry_src) {
                        return;
                    }

                    // See if that permute is a valid swizzle
                    let new_swizzle = match src_type {
                        SrcType::F16 => {
                            if combined != [0, 1, 0, 1] {
                                return;
                            }
                            SrcSwizzle::None
                        }
                        SrcType::F16v2 => match combined {
                            [0, 1, 2, 3] => SrcSwizzle::None,
                            [0, 1, 0, 1] => SrcSwizzle::Xx,
                            [2, 3, 2, 3] => SrcSwizzle::Yy,
                            _ => return,
                        },
                        _ => {
                            if combined != [0, 1, 2, 3] {
                                return;
                            }
                            SrcSwizzle::None
                        }
                    };

                    src.src_ref = entry_src.src_ref;
                    src.src_mod = entry_src.src_mod.modify(src.src_mod);
                    src.src_swizzle = new_swizzle;
                }
                CopyPropEntry::ConvIntToBool(entry) => {
                    // Fold i2b(b2i(x))
                    // Don't worry about CBuf rules, they cannot be used
                    // in bool-int-bool conversions as they cannot be dsts

                    // entry := i2b(current)
                    // find parent: parent := b2i(entry)
                    let parent =
                        entry.src.as_ssa().and_then(|x| self.get_copy(&x[0]));
                    let Some(CopyPropEntry::ConvBoolToInt(par_entry)) = parent
                    else {
                        return;
                    };

                    src.src_ref = par_entry.src.src_ref;
                    src.src_mod = par_entry.src.src_mod.modify(src.src_mod);
                    if entry.inverted {
                        src.src_mod = src.src_mod.bnot();
                    }
                }
                CopyPropEntry::ConvBoolToInt(_) => {
                    // b2i(i2b(x)) can't be easily optimized
                    return;
                }
            }
        }
    }

    fn prop_to_f64_src(&self, cbuf_rule: &CBufRule, src: &mut Src) {
        loop {
            let src_ssa = match &mut src.src_ref {
                SrcRef::SSA(ssa) => ssa,
                _ => return,
            };

            assert!(src_ssa.comps() == 2);

            // First, try to propagate the two halves individually.  Source
            // modifiers only apply to the high 32 bits so we have to reject
            // any copies with source modifiers in the low bits and apply
            // source modifiers as needed when propagating the high bits.
            let lo_entry_or_none = self.get_copy(&src_ssa[0]);
            if let Some(CopyPropEntry::Copy(lo_entry)) = lo_entry_or_none {
                if lo_entry.src.is_unmodified() {
                    if let SrcRef::SSA(lo_entry_ssa) = lo_entry.src.src_ref {
                        src_ssa[0] = lo_entry_ssa[0];
                        continue;
                    }
                }
            }

            let hi_entry_or_none = self.get_copy(&src_ssa[1]);
            if let Some(CopyPropEntry::Copy(hi_entry)) = hi_entry_or_none {
                if hi_entry.src.is_unmodified()
                    || hi_entry.src_type == SrcType::F64
                {
                    if let SrcRef::SSA(hi_entry_ssa) = hi_entry.src.src_ref {
                        src_ssa[1] = hi_entry_ssa[0];
                        src.src_mod = hi_entry.src.src_mod.modify(src.src_mod);
                        continue;
                    }
                }
            }

            let Some(CopyPropEntry::Copy(lo_entry)) = lo_entry_or_none else {
                return;
            };

            let Some(CopyPropEntry::Copy(hi_entry)) = hi_entry_or_none else {
                return;
            };

            if !lo_entry.src.is_unmodified() {
                return;
            }

            if !hi_entry.src.is_unmodified()
                && hi_entry.src_type != SrcType::F64
            {
                return;
            }

            if !cbuf_rule.allows_src(hi_entry.bi, &hi_entry.src)
                || !cbuf_rule.allows_src(lo_entry.bi, &lo_entry.src)
            {
                return;
            }

            let new_src_ref = match hi_entry.src.src_ref {
                SrcRef::Zero => match lo_entry.src.src_ref {
                    SrcRef::Zero | SrcRef::Imm32(0) => SrcRef::Zero,
                    _ => return,
                },
                SrcRef::Imm32(i) => {
                    // 32-bit immediates for f64 srouces are the top 32 bits
                    // with zero in the lower 32.
                    match lo_entry.src.src_ref {
                        SrcRef::Zero | SrcRef::Imm32(0) => SrcRef::Imm32(i),
                        _ => return,
                    }
                }
                SrcRef::CBuf(hi_cb) => match lo_entry.src.src_ref {
                    SrcRef::CBuf(lo_cb) => {
                        if hi_cb.buf != lo_cb.buf {
                            return;
                        }
                        if lo_cb.offset % 8 != 0 {
                            return;
                        }
                        if hi_cb.offset != lo_cb.offset + 4 {
                            return;
                        }
                        SrcRef::CBuf(lo_cb)
                    }
                    _ => return,
                },
                // SrcRef::SSA is already handled above
                _ => return,
            };

            src.src_ref = new_src_ref;
            src.src_mod = hi_entry.src.src_mod.modify(src.src_mod);
        }
    }

    fn prop_to_src(
        &self,
        src_type: SrcType,
        cbuf_rule: &CBufRule,
        src: &mut Src,
    ) {
        match src_type {
            SrcType::SSA => {
                self.prop_to_ssa_src(src);
            }
            SrcType::GPR => {
                self.prop_to_gpr_src(src);
            }
            SrcType::ALU
            | SrcType::F16
            | SrcType::F16v2
            | SrcType::F32
            | SrcType::I32
            | SrcType::B32
            | SrcType::Pred => {
                self.prop_to_scalar_src(src_type, cbuf_rule, src);
            }
            SrcType::F64 => {
                self.prop_to_f64_src(cbuf_rule, src);
            }
            SrcType::Carry | SrcType::Bar => (),
        }
    }

    fn try_add_instr(&mut self, bi: usize, instr: &Instr) {
        match &instr.op {
            Op::HAdd2(add) => {
                let dst = add.dst.as_ssa().unwrap();
                assert!(dst.comps() == 1);
                let dst = dst[0];

                if !add.saturate {
                    if add.srcs[0].is_fneg_zero(SrcType::F16v2) {
                        self.add_copy(bi, dst, SrcType::F16v2, add.srcs[1]);
                    } else if add.srcs[1].is_fneg_zero(SrcType::F16v2) {
                        self.add_copy(bi, dst, SrcType::F16v2, add.srcs[0]);
                    }
                }
            }
            Op::FAdd(add) => {
                let dst = add.dst.as_ssa().unwrap();
                assert!(dst.comps() == 1);
                let dst = dst[0];

                if !add.saturate {
                    if add.srcs[0].is_fneg_zero(SrcType::F32) {
                        self.add_copy(bi, dst, SrcType::F32, add.srcs[1]);
                    } else if add.srcs[1].is_fneg_zero(SrcType::F32) {
                        self.add_copy(bi, dst, SrcType::F32, add.srcs[0]);
                    }
                }
            }
            Op::DAdd(add) => {
                let dst = add.dst.as_ssa().unwrap();
                if add.srcs[0].is_fneg_zero(SrcType::F64) {
                    self.add_fp64_copy(bi, dst, add.srcs[1]);
                } else if add.srcs[1].is_fneg_zero(SrcType::F64) {
                    self.add_fp64_copy(bi, dst, add.srcs[0]);
                }
            }
            Op::Lop3(lop) => {
                let dst = lop.dst.as_ssa().unwrap();
                assert!(dst.comps() == 1);
                let dst = dst[0];

                let op = lop.op;
                if op.lut == 0 {
                    self.add_copy(bi, dst, SrcType::ALU, SrcRef::Zero.into());
                } else if op.lut == !0 {
                    self.add_copy(
                        bi,
                        dst,
                        SrcType::ALU,
                        SrcRef::Imm32(u32::MAX).into(),
                    );
                } else {
                    for s in 0..3 {
                        if op.lut == LogicOp3::SRC_MASKS[s] {
                            self.add_copy(bi, dst, SrcType::ALU, lop.srcs[s]);
                        }
                    }
                }
            }
            Op::PLop3(lop) => {
                for i in 0..2 {
                    let dst = match lop.dsts[i] {
                        Dst::SSA(vec) => {
                            assert!(vec.comps() == 1);
                            vec[0]
                        }
                        _ => continue,
                    };

                    let op = lop.ops[i];
                    if op.lut == 0 {
                        self.add_copy(
                            bi,
                            dst,
                            SrcType::Pred,
                            SrcRef::False.into(),
                        );
                    } else if op.lut == !0 {
                        self.add_copy(
                            bi,
                            dst,
                            SrcType::Pred,
                            SrcRef::True.into(),
                        );
                    } else {
                        for s in 0..3 {
                            if op.lut == LogicOp3::SRC_MASKS[s] {
                                self.add_copy(
                                    bi,
                                    dst,
                                    SrcType::Pred,
                                    lop.srcs[s],
                                );
                            } else if op.lut == !LogicOp3::SRC_MASKS[s] {
                                self.add_copy(
                                    bi,
                                    dst,
                                    SrcType::Pred,
                                    lop.srcs[s].bnot(),
                                );
                            }
                        }
                    }
                }
            }
            Op::Sel(sel) => {
                let dst = sel.dst.as_ssa().unwrap();
                assert!(dst.comps() == 1);
                let dst = dst[0];

                let src = match (sel.srcs[0], sel.srcs[1]) {
                    (z, u) if z.is_zero() && u.is_nonzero() => sel.cond.bnot(),
                    (u, z) if z.is_zero() && u.is_nonzero() => sel.cond,
                    _ => return,
                };

                self.add_b2i(dst, src);
            }
            Op::ISetP(isetp) if isetp.set_op.is_trivial(&isetp.accum) => {
                let dst = isetp.dst.as_ssa().unwrap();
                assert!(dst.comps() == 1);
                let dst = dst[0];

                let src = match (isetp.srcs[0], isetp.srcs[1]) {
                    (z, x) | (x, z) if z.is_zero() => x,
                    _ => return,
                };

                // -0 = 0
                // -x != 0 => x != 0
                if !matches!(src.src_mod, SrcMod::None | SrcMod::INeg) {
                    return;
                }

                // x op 0
                let inverted = match isetp.cmp_op {
                    IntCmpOp::Eq => true,
                    IntCmpOp::Ne => false,
                    _ => return,
                };
                self.add_i2b(dst, src, inverted);
            }
            Op::IAdd2(add) => {
                let dst = add.dst.as_ssa().unwrap();
                assert!(dst.comps() == 1);
                let dst = dst[0];

                if add.srcs[0].is_zero() {
                    self.add_copy(bi, dst, SrcType::I32, add.srcs[1]);
                } else if add.srcs[1].is_zero() {
                    self.add_copy(bi, dst, SrcType::I32, add.srcs[0]);
                }
            }
            Op::IAdd3(add) => {
                let dst = add.dst.as_ssa().unwrap();
                assert!(dst.comps() == 1);
                let dst = dst[0];

                if add.srcs[0].is_zero() {
                    if add.srcs[1].is_zero() {
                        self.add_copy(bi, dst, SrcType::I32, add.srcs[2]);
                    } else if add.srcs[2].is_zero() {
                        self.add_copy(bi, dst, SrcType::I32, add.srcs[1]);
                    }
                } else if add.srcs[1].is_zero() && add.srcs[2].is_zero() {
                    self.add_copy(bi, dst, SrcType::I32, add.srcs[0]);
                }
            }
            Op::Prmt(prmt) => {
                let dst = prmt.dst.as_ssa().unwrap();
                assert!(dst.comps() == 1);
                if let Some(sel) = prmt.get_sel() {
                    if let Some(imm) = prmt.as_u32() {
                        self.add_copy(bi, dst[0], SrcType::GPR, imm.into());
                    } else if sel == PrmtSel(0x3210) {
                        self.add_copy(bi, dst[0], SrcType::GPR, prmt.srcs[0]);
                    } else if sel == PrmtSel(0x7654) {
                        self.add_copy(bi, dst[0], SrcType::GPR, prmt.srcs[1]);
                    } else {
                        self.add_prmt(bi, dst[0], sel, prmt.srcs);
                    }
                }
            }
            Op::R2UR(r2ur) => {
                assert!(r2ur.src.is_unmodified());
                if r2ur.src.is_uniform() {
                    let dst = r2ur.dst.as_ssa().unwrap();
                    assert!(dst.comps() == 1);
                    self.add_copy(bi, dst[0], SrcType::GPR, r2ur.src);
                }
            }
            Op::Copy(copy) => {
                let dst = copy.dst.as_ssa().unwrap();
                assert!(dst.comps() == 1);
                self.add_copy(bi, dst[0], SrcType::GPR, copy.src);
            }
            Op::ParCopy(pcopy) => {
                for (dst, src) in pcopy.dsts_srcs.iter() {
                    let dst = dst.as_ssa().unwrap();
                    assert!(dst.comps() == 1);
                    self.add_copy(bi, dst[0], SrcType::GPR, *src);
                }
            }
            _ => (),
        }
    }

    pub fn run(&mut self, f: &mut Function) {
        for (bi, b) in f.blocks.iter_mut().enumerate() {
            let b_uniform = b.uniform;
            for instr in &mut b.instrs {
                self.try_add_instr(bi, instr);

                self.prop_to_pred(&mut instr.pred);

                let cbuf_rule = if instr.is_uniform() {
                    CBufRule::No
                } else if !b_uniform {
                    CBufRule::BindlessRequiresBlock(bi)
                } else {
                    CBufRule::Yes
                };

                // Carry-out and overflow interact funny with SrcMod::INeg so we
                // can only propagate with modifiers if no carry/overflow is
                // written.
                let force_alu_src_type = match &instr.op {
                    Op::IAdd2(add) => !add.carry_out.is_none(),
                    Op::IAdd2X(add) => !add.carry_out.is_none(),
                    Op::IAdd3(add) => {
                        !add.overflow[0].is_none() || !add.overflow[1].is_none()
                    }
                    Op::IAdd3X(add) => {
                        !add.overflow[0].is_none() || !add.overflow[1].is_none()
                    }
                    Op::Lea(lea) => !lea.overflow.is_none(),
                    Op::LeaX(lea) => !lea.overflow.is_none(),
                    _ => false,
                };

                let src_types = instr.src_types();
                for (i, src) in instr.srcs_mut().iter_mut().enumerate() {
                    let mut src_type = src_types[i];
                    if force_alu_src_type {
                        src_type = match src_type {
                            SrcType::ALU | SrcType::B32 | SrcType::I32 => {
                                SrcType::ALU
                            }
                            SrcType::Carry | SrcType::Pred => src_type,
                            _ => panic!("Unhandled src_type"),
                        };
                    };
                    self.prop_to_src(src_type, &cbuf_rule, src);
                }
            }
        }
    }
}

impl Shader<'_> {
    pub fn opt_copy_prop(&mut self) {
        for f in &mut self.functions {
            CopyPropPass::new().run(f);
        }
    }
}
