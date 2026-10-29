#include "reset_journal.h"
#include <Preferences.h>
#include <assert.h>
#include <stdio.h>
int main(){
 assert(reset_journal::state()==reset_journal::State::Ready);assert(reset_journal::mark(true));
 assert(reset_journal::state()==reset_journal::State::Pending); // New boot: the persisted marker survives object lifetimes.
 writeFailure=true;assert(!reset_journal::mark(false));assert(reset_journal::state()==reset_journal::State::Pending);
 writeFailure=false;assert(reset_journal::mark(false));assert(reset_journal::state()==reset_journal::State::Ready);
 openFailure=true;assert(reset_journal::state()==reset_journal::State::Unavailable);assert(!reset_journal::mark(true));
 puts("reset intent survives restart; failed completion/open keeps output locked");
}
