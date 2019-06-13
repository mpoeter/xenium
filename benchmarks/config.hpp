#pragma once

#define QUEUE_ITEM std::uint32_t

// Many configuration paramters are compile time parameters. Therefore, every
// additional configuration increases compile time. In order to keep compile
// times to a minimum these macros can be used to exclude specific data structures
// or reclamation schemes.

// defines which data structures shall be included
#define WITH_MICHAEL_SCOTT_QUEUE
#define WITH_RAMALHETE_QUEUE
#define WITH_VYUKOV_BOUNDED_QUEUE

// defines which reclamation schemes shall be included
#define WITH_EPOCH_BASED
#define WITH_NEW_EPOCH_BASED
#define WITH_QUIESCENT_STATE_BASED
#define WITH_DEBRA
