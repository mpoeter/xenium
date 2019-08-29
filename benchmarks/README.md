# Benchmarks
This is a flexible benchmark framework that allows to define simple synthetic
benchmarks, as well as (almost) arbitrarily complex benchmark scenarios. At the
moment it contains only a couple of benchmarks for the concurrent data structures
implemented in `xenium`, but there are plans to extend these benchmarks to cover
other libraries like `libcds` as well, so we can get comparable results.

## Design
The design goal was to be able to easily run each benchmark with varying values
for both, runtime as well as compile time parameters. This is achieved by using
template meta programming techniques to compile each benchmark for all configured
variations of compile time parameters. At runtime, the given configuration is
compared against the available configurations of compile time parameters, looking
for a matching benchmark to be executed.

The downside of this approach is that with a large number of compile time parameter
variations, the compile times can get quite high, and the compiled executable can
become quite large. On the other hand, once compiled it allows to run a large number
of parameter combinations in an automated way (e.g., via a shell script).

## Configuration
The benchmark execution is based on a JSON configuration file like the following:
```json
{
  "type": "queue",
  "ds": {
    "type": "michael_scott_queue",
    "reclaimer": {
      "type": "epoch_based",
      "update_threshold": 100

    }
  },
  "rounds": 5,
  "runtime": 500
  "threads": {
    "producer": {
      "count": 4,
      "pop_ratio": 0.1
    },
    "consumer": {
      "count": 4,
      "push_ratio": 0.1
    }
  }
}
```
The [configuration documentation](config.md) contains more details.