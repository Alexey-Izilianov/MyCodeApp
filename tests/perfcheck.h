#pragma once

#include <QtTest>

// Проверка скорости. Под санитайзерами код в разы медленнее, там CI задаёт
// MYCODEAPP_NO_PERF — замер выполняется, но порог не проверяется
#define PERF_VERIFY2(condition, message)                                   \
    do {                                                                   \
        if (!qEnvironmentVariableIsSet("MYCODEAPP_NO_PERF"))               \
            QVERIFY2(condition, message);                                  \
    } while (false)
