# USBIP preparation and lifecycle evidence

Use the existing simulator front door:

```powershell
tools/ymcc_sim/ymcc_sim.ps1 -Domain global -Mode run -Tests 'controller.usbip-backend-preparation,controller.usbip-setup-evidence'
```

The first case executes the actual production advisory factory, driver preparation and USBIP preparation bodies. It exercises already available, install success, install failure, initial/post-install probe exceptions, factory exceptions and a non-SD control. Compiled faults must catch a dropped progress callback and an incorrect gate that skips the target factory after a preparation warning.

The second case exercises the actual bounded evidence sink with declared registry/CM read leaves and a private filesystem. It does not load the SDK, execute its transport, install anything or create a controller.

Source and toolchain identities belong to each fresh result. Software evidence does not establish actual active driver version, attach, input consumption, forced-reboot recovery or physical sleep stability. Those remain part of the next G3E field round.
