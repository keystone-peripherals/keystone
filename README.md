# Anonymous Code Submission

This branch holds the anonymized code submission for our Usenix 2025 submission
titled "Flexible Peripheral Sharing for Keystone Enclaves". We include a brief
guide for the code that we have added in our work.

## Infrastructure

We include a brief guide to *using* our code, if desired.

### Cloning

First, clone this repository, checkout the correct branch, and get the submodules:

```
git clone https://github.com/keystone-peripherals/keystone
cd keystone ; git checkout review-usenix-2025
git submodule update --init --recursive
```

### Patching

We do require a tiny patch to Buildroot to ensure that the very last image
generation step in the build completes successfully. To apply this patch, do:

```
cd buildroot
cat ../0001-fix-buildroot-paths.patch | git apply
```

### Building

We support builds mainly for the MPFS platform (i.e., our Polarfire Icicle kit).
To build for this platform, run the following command from the `keystone/` directory:

```
KEYSTONE_PLATFORM=mpfs make
```

Be aware that this build can take quite a while, consume many dozens of gigabytes
of storage space, and OOM your system if run at too high of parallelism. We can
provide prebuilt images for all relevant components if requested.

### Testing / Reproducing

Testing our changes requires quite a detailed hardware setup and flashing procedure.
If requested by reviewers, we can make a new out-of-the-box Polarfire Icicle kit
available remotely and provide assistance in reproducing our results.

## New functionality

We also point out the locations of various interesting components and changes from
our paper, to make code review easier.

### Device Access

The SM changes are contained primarily in `sm/src/device.{c,h}`. There is also some
added functionality in `sm/src/enclave.c`, primarily around the `claim_mmio` and
`release_mmio` SBI calls. These SBI calls are then made available to eapps by various
glue syscalls in the runtime and SDK, which we do not explicitly point out.

### Message Passing

As discussed in the paper, implementing message passing required changes across the
Keystone system stack. We highlight code changes closely following the flow in the paper.
The primary source files of interest are `sm/src/enclave.c`, `runtime/call/callee.c`,
`sdk/src/app/callee.c`, and `runtime/sys/thread.c`.

The `register_handler` SBI call is implemented in `sm/src/enclave.c`, and invoked by the
runtime in the `callee_init` function in `runtime/call/callee.c`.

The eapp message passing functionality (both for callers and callee's) is actually
implemented in the SDK, to make it easier to link against. The core source file holding
this implementation is in `sdk/src/app/callee.c`. To spawn a new callee execution environment,
a caller would invoke `spawn_callee_handler` from this file.

The runtime threading component to support message passing is in `runtime/sys/thread.c`,
and runtime-level message-passing support (including the implementation of `clone`)
is in `runtime/call/callee.c`.

When the caller requests a message pass, it invokes the SM's `call_enclave` function
which is defined in `sm/src/enclave.c`. When this function returns in the callee's
execution context, we resume in the callee's runtime at `encl_call_handler` in the
`runtime/call/callee.S` file. This function then eventually returns to the
`dispatch_callee_handler` function in the callee's eapp context, in `sdk/src/app/callee.c`.

### Shared Subregions

The SBI calls implementing shared regions are in `sm/src/enclave.c`, and are named
`share_region` and `unshare_region`. These then call out to subregioning functions in
`sm/src/pmp.c`, which implement the running-minimum algorithm described in the paper's
appendix. This file also implements the `pmp_move_global` function, which implements
the described PMP movement/invalidation flow.

### Function export

Most of the function export functionality lives alongside the message passing functionality
described previously. Of note is the build system, which is implemented in the
`add_exportable_function` CMake macro in `sdk/macros.cmake`. For examples of how this
macro is used, please refer to our macrobenchmark examples (described further below).

### Testbench

The hardware work to integrate VTA into the Polarfire SoC reference design is not
contained in this repository, but is available by request.

The software work to integrate TVM with RISC-V and the broader Keystone infrastructure
is primarily contained in `overlays/tvm`, following standard conventions for a
[Buildroot external overlay](https://buildroot.org/downloads/manual/manual.html)
(Section 9.2). This directory contains some packages that wrap the TVM build, along
with patch files to add RISC-V support and break out certain static libraries. Other
patch files (primarily in `overlays/tvm/patches`) are used to relocate firmwares to
create larger CMA areas as discussed in the Appendix.

### Microbenchmarks

Microbenchmarks are implemented in `examples/profile`, along with several plumbing
changes in the runtime (guarded by the `callee_profile` feature), so that timestamps
can be collected as closely as possible to regions of interest.

### Macrobenchmarks

Macrobenchmarks are implemented in `examples/vta`.

### Data analysis

Data analysis scripts used to parse the outputs of our micro- and macrobenchmarks
can be made available as requested.
