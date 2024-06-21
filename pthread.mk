include ../config.mk
NEW_ALLOC=1
MEM_ALIGN=64

ifdef SSESPEED
 MEM_COPY_SSE2=1
endif

PT_TUNE=$(TUNE)
