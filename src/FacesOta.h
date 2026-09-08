#pragma once
namespace faces_ota {
void begin();
// True when an installation attempt returned without rebooting.
bool tick(bool idle);
void acceptBoot(bool healthy);
}
