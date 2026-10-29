#include "reset_journal.h"
#include <Preferences.h>
namespace reset_journal {
bool mark(bool pending) {
 Preferences journal;
 if(!journal.begin("uc-core",false))return false;
 bool ok=journal.putBool("reset",pending)==sizeof(bool)&&journal.getBool("reset",!pending)==pending;
 journal.end();return ok;
}
State state() {
 Preferences journal;
 // A storage error must not restore old output settings after a requested reset.
 if(!journal.begin("uc-core",false))return State::Unavailable;
 bool pending=journal.getBool("reset",false);journal.end();return pending?State::Pending:State::Ready;
}
}
