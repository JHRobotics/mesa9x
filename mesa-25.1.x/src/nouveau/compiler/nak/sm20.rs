// Copyright © 2025 Collabora, Ltd.
// SPDX-License-Identifier: MIT

use crate::ir::*;
use crate::legalize::LegalizeBuilder;
use bitview::{BitMutView, BitMutViewable, BitView, BitViewable, SetFieldU64};

use std::{collections::HashMap, ops::Range};

pub struct ShaderModel20 {
    sm: u8,
}

impl ShaderModel20 {
    pub fn new(sm: u8) -> Self {
        assert!(sm >= 20 && sm < 32);
        Self { sm }
    }
}

impl ShaderModel for ShaderModel20 {
    fn sm(&self) -> u8 {
        self.sm
    }

    fn num_regs(&self, file: RegFile) -> u32 {
        match file {
            RegFile::GPR => 255,
            RegFile::UGPR => 0,
            RegFile::Pred => 7,
            RegFile::UPred => 0,
            RegFile::Carry => 1,
            RegFile::Bar => 0,
            RegFile::Mem => RegRef::MAX_IDX + 1,
        }
    }

    fn hw_reserved_gprs(&self) -> u32 {
        0
    }

    fn crs_size(&self, max_crs_depth: u32) -> u32 {
        if max_crs_depth <= 16 {
            0
        } else if max_crs_depth <= 32 {
            1024
        } else {
            ((max_crs_depth + 32) * 16).next_multiple_of(512)
        }
    }

    fn op_can_be_uniform(&self, _op: &Op) -> bool {
        false
    }

    fn exec_latency(&self, op: &Op) -> u32 {
        // TODO
        match op {
            Op::CCtl(_)
            | Op::MemBar(_)
            | Op::Bra(_)
            | Op::SSy(_)
            | Op::Sync(_)
            | Op::Brk(_)
            | Op::PBk(_)
            | Op::Cont(_)
            | Op::PCnt(_)
            | Op::Exit(_)
            | Op::Bar(_)
            | Op::Kill(_)
            | Op::OutFinal(_) => 13,
            _ => 1,
        }
    }

    fn raw_latency(
        &self,
        _write: &Op,
        _dst_idx: usize,
        _read: &Op,
        _src_idx: usize,
    ) -> u32 {
        // TODO
        13
    }

    fn war_latency(
        &self,
        _read: &Op,
        _src_idx: usize,
        _write: &Op,
        _dst_idx: usize,
    ) -> u32 {
        // TODO
        // We assume the source gets read in the first 4 cycles.  We don't know
        // how quickly the write will happen.  This is all a guess.
        4
    }

    fn waw_latency(
        &self,
        _a: &Op,
        _a_dst_idx: usize,
        _a_has_pred: bool,
        _b: &Op,
        _b_dst_idx: usize,
    ) -> u32 {
        // We know our latencies are wrong so assume the wrote could happen
        // anywhere between 0 and instr_latency(a) cycles

        // TODO
        13
    }

    fn paw_latency(&self, _write: &Op, _dst_idx: usize) -> u32 {
        // TODO
        13
    }

    fn worst_latency(&self, _write: &Op, _dst_idx: usize) -> u32 {
        // TODO
        15
    }

    fn legalize_op(&self, b: &mut LegalizeBuilder, op: &mut Op) {
        as_sm20_op_mut(op).legalize(b);
    }

    fn encode_shader(&self, s: &Shader<'_>) -> Vec<u32> {
        encode_sm20_shader(self, s)
    }
}

trait SM20Op {
    fn legalize(&mut self, b: &mut LegalizeBuilder);
    #[allow(dead_code)]
    fn encode(&self, e: &mut SM20Encoder<'_>);
}

#[allow(dead_code)]
struct SM20Encoder<'a> {
    sm: &'a ShaderModel20,
    ip: usize,
    labels: &'a HashMap<Label, usize>,
    inst: [u32; 2],
    sched: u8,
}

impl BitViewable for SM20Encoder<'_> {
    fn bits(&self) -> usize {
        BitView::new(&self.inst).bits()
    }

    fn get_bit_range_u64(&self, range: Range<usize>) -> u64 {
        BitView::new(&self.inst).get_bit_range_u64(range)
    }
}

impl BitMutViewable for SM20Encoder<'_> {
    fn set_bit_range_u64(&mut self, range: Range<usize>, val: u64) {
        BitMutView::new(&mut self.inst).set_bit_range_u64(range, val);
    }
}

impl SetFieldU64 for SM20Encoder<'_> {
    fn set_field_u64(&mut self, range: Range<usize>, val: u64) {
        BitMutView::new(&mut self.inst).set_field_u64(range, val);
    }
}
macro_rules! as_sm20_op_match {
    ($op: expr) => {
        match $op {
            _ => panic!("Unhandled instruction {}", $op),
        }
    };
}

#[allow(dead_code)]
fn as_sm20_op(op: &Op) -> &dyn SM20Op {
    as_sm20_op_match!(op)
}

fn as_sm20_op_mut(op: &mut Op) -> &mut dyn SM20Op {
    as_sm20_op_match!(op)
}

fn encode_sm20_shader(_sm: &ShaderModel20, _s: &Shader<'_>) -> Vec<u32> {
    todo!("Implement SM20 encoding");
}
