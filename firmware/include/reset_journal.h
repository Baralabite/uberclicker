#pragma once
namespace reset_journal {
enum class State { Ready, Pending, Unavailable };
State state();
bool mark(bool pending);
}
