.. _profiling-perf:

Perf
####

Perf is a profiler tool based on stack tracing. It can be used for lightweight profiling
with minimal code overhead.

Work Principle
**************

The ``perf record`` shell command starts a timer with the perf tracer function.
Timers are driven by interrupts, so the perf tracer function is called during an interruption.
The Zephyr core saves the return address and frame pointer in the interrupt stack,
``callee_saved`` structure, or architecture-specific exception frame before calling the
interrupt handler. Thus, the perf trace function makes stack traces by using the return
address and frame pointer.

On Cortex-M, perf wraps the SysTick handler so it can sample the interrupted
Thread-mode Process Stack Pointer (PSP) frame before the normal timer ISR uses the
handler stack. The backend validates that frame before passing it to the Arm stack
walker.

The Cortex-M backend is unavailable for Non-secure Trusted Execution images because
Secure exception frames are inaccessible to Non-secure firmware.

On Xtensa, the interrupt handlers keep the stack that each interrupt interrupted while they run.
The backend spills the register windows and walks that stack with the architecture's backtrace
code, so a sample starts at the interrupted instruction.

On SMP systems with IPI support, the CPU the timer fires on samples itself and sends an IPI to the
other CPUs, which sample themselves in the IPI handler.

The :zephyr_file:`scripts/profiling/stackcollapse.py` script can be used to convert return addresses
in the stack trace to function names using symbols from the ELF file, and to prints them in the
format expected by `FlameGraph`_.

Configuration
*************

You can configure this module using the following options:

* :kconfig:option:`CONFIG_PROFILING_PERF`: Enables the module. This option adds
  the ``perf`` command to the shell.

* :kconfig:option:`CONFIG_PROFILING_PERF_BUFFER_SIZE`: Sets the size of the perf buffer
  where samples are saved before printing.

Architecture backends may require additional stack-unwind support. The Cortex-M backend
requires SysTick, thread stack information, extra exception information, Arm stack
walking support, and a uniprocessor configuration. The Xtensa backend requires
:kconfig:option:`CONFIG_XTENSA_ENABLE_BACKTRACE`.

Usage
*****

``perf record <duration> <frequency> [thread_id [cpu_mask]]`` samples for ``duration``
milliseconds at ``frequency`` hertz. ``thread_id``, in hexadecimal, keeps the samples of one
thread; ``0`` keeps all threads. ``cpu_mask``, in hexadecimal, selects the CPUs to sample and
defaults to all of them. A CPU outside the mask is neither sampled nor sent an IPI, so code with
tight timing can run undisturbed on it while the other CPUs are profiled; the timer interrupt
still fires on its own CPU. ``perf printbuf`` prints the samples and clears the buffer.

Refer to the :zephyr:code-sample:`profiling-perf` sample for an example of how to use the perf tool.

 .. _FlameGraph: https://github.com/brendangregg/FlameGraph/
