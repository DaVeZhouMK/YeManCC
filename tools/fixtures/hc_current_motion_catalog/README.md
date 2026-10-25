This fixture executes the pinned upstream DeviceConfigurationHelper and
DeviceConfiguration models, plus verbatim IDevice ApplyDeviceConfiguration,
ConvertToIMUMatrix and IMUMatrix code, and verbatim gyro/accel numeric callbacks
with data-only event-argument types and a no-op base notification. Only the class hierarchy is represented
by inert types with the exact original full names and inheritance. It does not
execute real HC device constructors, HC App, sensors, controllers or services.

All 53 resource JSON files are copied only to isolated test output. The fixture
also runs controlled missing/corrupt resource cases there. Native identity and
matrix output are independently executed from current production function
bodies. Differences are source-level compatibility facts, not physical axis
verification or permission to overwrite proven MSI/ROG/GPD routes.
