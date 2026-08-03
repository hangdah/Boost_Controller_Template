# Repository Guidelines

## Project Structure & Module Organization

This repository is a Code Composer Studio (CCS) project for the TMS320F28335 boost-converter controller. `main.c` owns system startup and the foreground loop. Application peripherals and control logic live in `APP/` (`adc.c`, `epwm.c`, `mppt.c`, timers, LED, and helper functions), with public declarations in `APP/APP_Libraries/`. Device-support sources, linker command files, assembly startup code, and `IQmath.lib` are under `DSP2833x_Librsries/`. CCS metadata is stored in `.project`, `.cproject`, and `.ccsproject`; the target connection is defined by `targetConfigs/TMS320F28335.ccxml`. Treat `Debug/` as generated build output.

## Build, Test, and Development Commands

- Import the repository in CCS with **File > Import > CCS Projects**, select the `Debug` or `Release` configuration, then use **Project > Build Project**.
- `gmake -C Debug all` builds from a CCS-enabled command prompt using the generated makefiles.
- `gmake -C Debug clean` removes generated Debug artifacts.

The project expects TI C2000 Compiler `22.6.1.LTS`. Generated makefiles contain machine-specific absolute paths, so prefer rebuilding through CCS after moving the repository; do not hand-edit files under `Debug/`.

## Coding Style & Naming Conventions

Follow the existing embedded-C style: four-space indentation, braces on the next line for functions, and one hardware responsibility per module. Preserve established names and TI types such as `Uint16`. Public peripheral APIs use module-oriented names (`ADC_Init`, `EPWM1_Init`); internal state should remain `static` where possible. Match each new `APP/foo.c` with `APP/APP_Libraries/foo.h` and an uppercase include guard. Keep register writes localized and pair protected-register access with `EALLOW`/`EDIS`. Avoid broad formatting changes and add new comments in English.

## Testing Guidelines

No automated test framework or coverage target is present. At minimum, build without new compiler or linker diagnostics. For control, ADC, PWM, timer, or interrupt changes, load the `.out` file through CCS and document bench checks: target revision, input limits, observed PWM frequency/duty cycle, ADC readings, and trip-zone behavior. Begin testing with PWM output disabled and current-limited power.

## Commit & Pull Request Guidelines

No Git history is available in this checkout, so use concise imperative commits, for example `Fix EPWM SOCB trigger selection`. Keep commits scoped to one behavior. Pull requests should describe the control or peripheral impact, list changed modules, include build and hardware-test results, and call out modifications to linker, target, register, interrupt, ADC, or PWM configuration. Attach scope captures or CCS screenshots when waveform or timing behavior changes.
