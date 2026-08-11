#pragma once

#include "ManagementPage.h"

namespace Pages {
ManagementPage *create(const QString &nodeId, QWidget *parent = nullptr);
}

