#pragma once

#include <stdbool.h>

#include "AppHeader.h"

bool IsOtaRequested(void);

bool OtaRequestSet(void);

bool OtaRequestCommitHeader(const AppHeader_t *header);