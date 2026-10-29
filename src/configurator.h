#ifndef CROSE_CONFIGURATOR_H
#define CROSE_CONFIGURATOR_H

#include "pet_config.h"

namespace crose
{
    // Opens the cross-platform startup editor. Returns true when the user chose Run Pet.
    bool runConfigurator(Config &config);
}

#endif
