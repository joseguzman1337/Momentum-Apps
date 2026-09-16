#include <flipper_application/api_hashtable/api_hashtable.h>
#include <flipper_application/api_hashtable/compilesort.hpp>

/* 
 * This file contains an implementation of a symbol table 
 * with private app's symbols. It is used by composite API resolver
 * to load plugins that use internal application's APIs.
 */
#include "metroflip_api_table_i.h"

static_assert(!has_hash_collisions(metroflip_api_table), "Detected API method hash collision!");
static constexpr auto metroflip_packed_api_table = pack_hashtable(metroflip_api_table);

constexpr HashtableApiInterface applicaton_hashtable_api_interface{
    {
        .api_version_major = 0,
        .api_version_minor = 0,
        /* generic resolver using pre-sorted array */
        .resolver_callback = &elf_resolve_from_hashtable,
    },
    metroflip_packed_api_table.hash_low.data(),
    metroflip_packed_api_table.hash_high.data(),
    metroflip_packed_api_table.addresses.data(),
    metroflip_api_table.size(),
    metroflip_packed_api_table.low_width,
};

/* Casting to generic resolver to use in Composite API resolver */
extern "C" const ElfApiInterface* const metroflip_api_interface =
    &applicaton_hashtable_api_interface;
