# StackChan attribution

The companion's geometry and animation behavior are adapted from the default
StackChan skin at https://github.com/m5stack/StackChan (firmware revision
`1b5765599fba8aaad1811d9a79358ccc7051f5f3`, MIT). The applicable license is
retained in `LICENSE`.

`CompanionModel.h` and `companion.cpp` implement an independent M5GFX renderer
for the Faces/CoreS3 screen. The surrounding app adds local touch, IMU,
proximity, keyboard, and launcher behavior. It does not include the StackChan
OS, cloud services, mobile/WebSocket service, servo control, or emoji assets.

The local motion signal is coarse temporal image difference. It is not face
recognition or a calibrated distance measurement.
