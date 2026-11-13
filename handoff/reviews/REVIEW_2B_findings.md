# Review of order 2B (shadow oracle), lead's read of src/nvm/tier_shadow.inc (rebased on fcc8c3e)

Read in full: tier_shadow.inc. Not yet read: hooks in machine.cpp/.hpp, tier1.*, runner.cpp, tests, scripts.

Findings to send back (none is a soundness defect of the run; all are gaps in what the oracle compares):
1. Scheduler comparison is partial. Not compared: Task::{parent, parent_scope, context, unjoinable, spawn_function,
   spawn_block, result_type, close_on_wake, wait_value_type, select (arms), selects, stash_type, pending,
   cancellable, cancel_sent}, the contents of Task::scopes (only the count), Scheduler::{next_id, next_scope,
   next_channel, next_timer, next_guard, peak_live, peak_requests}, and the contents (order) of Channel::senders,
   Channel::receivers, Timer::waiters, Guard::waiters (only sizes). Either compare them or state in the header
   comment exactly which fields are not compared and why that cannot hide a divergence. Profile-only fields
   (started, steps, since_tick, runnable_ticks, sites) need limits_.profile like function_steps.
2. Still to check by me: shadowed_advance duplicates advance(); test that greps or asserts they stay equal?
3. Still to run by me: tests, cost measurements, one mutation (e.g. make a region store a wrong slot) caught.
4. After the rebase onto main: RunLimits gained `block_observer` (coverage). start_shadow() copies the run's limits and
   clears observer/task_dump/schedule/chooser but not block_observer, so a shadow would report every block a second time
   to the observer if a run ever had both. Clear it in start_shadow() and add a test (observer call count with and
   without the oracle).
Read now: machine.cpp/.hpp hooks, tier1.*, runner.cpp (no further finding). Not yet read: tests, scripts, docs.
