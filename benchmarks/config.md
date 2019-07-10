# General

The configuration files are JSON with the following basic structure:
```json
{
  "type": string,
  "ds": <data-structure>,
  "threads": <benchmark-type::threads>,
  "warmup": <warmup> (optional),
  "runtime": integer (in ms; optional),
  "rounds": integer (optional),

  <type-specific-params...>
}
```
`type` defines the type of the benchmark; most of the other parameters depend
on the value of `type`. At the moment, the only supported type is `queue`.

`ds` defines the data structure to be used; the possible values depend on the
specified benchmark type.
Many data structures are designed to use template paramters for compile time
configuration. One can define which configuration variations for each data
structure should be built for the benchmark. At runtime, the configuration
specified in `ds` is compared to the available configurations in order to run
the benchmark with the correctly configured data structure.
This is a sample `ds` configuration:
```json
  {
    "type": "michael_scott_queue",
    "reclaimer": {
      "type": "epoch_based"
    }
  }
```
It always requires a `type` field that specifies the data structure type. The
other parameters depend on that type. To get a list of available benchmarks
with their respective `ds` configurations, one can run the benchmark executable
with `--help`.

`threads` defines the number and types of threads. The thread
configuration looks like this:
```json
{
  <name>: {
   "count": 4,
   "type": string (optional; defaults to <name>),
   <type-specific-params>
  },
  <more threads...>
}
```
It is an oject consisting of an arbitrary number of name/value pairs. This
allows one to use different kinds of threads like producers/consumers with
different configurations. The type of the thread defaults to the given
`<name>`, but it is also possible to use a user defined name, and specify the
type explicitly. This is useful when you have multiple configurations of the
same type.

`warmup` defines the number of warmup rounds and the runtime of each warmup round:
```json
{
  "rounds": integer (optional; defaults to 0),
  "runtime": integer (in ms; optional; defaults to 5000)
}
```
The `warmup` parameter as well as both members are optional. Unless specified,
`rounds` defaults to zero and `runtime` defaults to 5000; the `runtime` is
specified in milliseconds. Warmup rounds are executed just the same as the normal
rounds, but they are not considered in the result report.

`runtime` defines the runtime of each round in milliseconds; defaults to 10000.

`rounds` defines the number of rounds to be executed. Each round has its own set
of threads, i.e., the configured threads are started, and once all threads are
up, the execution begins. Once the runtime expires, all threads are stopped, and
a report for the round is created, containing informations like actual runtime
and number of executed operations.

# Benchmarks

## Queue

This is a simple synthetic benchmark for the different queues:
  * `michael_scott_queue`
  * `ramalhete_queue`
  * `vyukov_bounded_queue`

### General

`prefill` defines the number of items the queue should be prefilled with before
starting each round.

### Data structure

This is a list of the supported queue data structures with their respective
properties. For details about the various parameters please consult the
[documentation](https://mpoeter.github.io/xenium). Some of the data structures
require a reclaimer to be configured. For a list of available reclaimers and
their configurations see section "Reclaimers".

**`vyukov_bounded_queue`**
```json
{
  "type": "vyukov_bounded_queue",
  "weak": boolean,
  "size": integer (has to be a power to 2; is a runtime parameter)
}
```

**`ramalhete_queue`**
```json
{
  "type": "ramalhete_queue",
  "reclaimer": <reclaimer>
}
```

**`michael_scott_queue`**
```json
{
  "type": "michael_scott_queue",
  "reclaimer": <reclaimer>
}
```

### Threads

**`producer`** defines threads that _push_ values into the queue.
```json
{
  "count": integer,
  "pop_ratio": float (optional; defaults to 0.0),
  "workload": <workload> | integer | (optional; defaults to `nothing`)
}
```
`pop_ratio` can be used to define how many pop operations the thread should
perform.

**`consumer`** defines threads that _pop_ values from the queue.
```json
{
  "count": integer,
  "push_ratio": float (optional; defaults to 0.0),
  "workload": <workload> | integer | (optional; defaults to `nothing`)
}
```
`push_ratio` can be used to define how many push operations the thread should
perform.


`workload` defines a virtual workload that a thread has to perform between
each push/pop operation. This value can be a simple integer, in which case
it defines the number of iterations for the `dummy` workload. Otherwise this
defines a workload object.

# Reclaimers

Many data structures require specification of a `reclaimer`. This is a list
of supported reclaimers with their respective properties:

**`epoch_based`**
```json
{
  "type": "epoch_based",
  "update_threshold": integer
}
```

**`new_epoch_based`**
```json
{
  "type": "new_epoch_based",
  "update_threshold": integer
}
```

**`quiescent_state_based`**
```json
{
  "type": "quiescent_state_based"
}
```

**`debra`**
```json
{
  "type": "debra",
  "update_threshold": integer
}
```

`hazard_pointer`
```json
{
  "type": "hazard_pointer",
  "policy": {
    "type": "static_hazard_pointer_policy" | "dynamic_hazard_pointer_policy",
    "K": integer,
    "A": integer,
    "B": integer
  }
}
```
