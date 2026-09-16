#include <flipper_application/api_hashtable/api_hashtable.h>
#include <flipper_application/api_hashtable/compilesort.hpp>
#include "app_api_interface.h"
#include "app_api_table_i.h"

static_assert(!has_hash_collisions(app_api_table), "Detected API method hash collision!");
static constexpr auto app_packed_api_table = pack_hashtable(app_api_table);

constexpr HashtableApiInterface applicaton_hashtable_api_interface{
    {
        .api_version_major = 0,
        .api_version_minor = 0,
        .resolver_callback = &elf_resolve_from_hashtable,
    },
    app_packed_api_table.hash_low.data(),
    app_packed_api_table.hash_high.data(),
    app_packed_api_table.addresses.data(),
    app_api_table.size(),
    app_packed_api_table.low_width,
};

extern "C" const ElfApiInterface* const application_api_interface =
    &applicaton_hashtable_api_interface;
