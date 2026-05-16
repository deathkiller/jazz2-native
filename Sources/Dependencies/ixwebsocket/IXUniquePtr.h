/*
 *  IXUniquePtr.h
 *  Author: Benjamin Sergeant
 *  Copyright (c) 2020 Machine Zone, Inc. All rights reserved.
 */

#pragma once

#include <memory>

namespace ix
{
    /**
     * @brief Utility for creating `std::unique_ptr` (C++14/17 compatibility).
     *
     * Implements a `make_unique` function for environments lacking it. Used internally for safe pointer management.
     * @tparam T Type to construct
     * @tparam Args Constructor argument types
     * @param args Arguments to forward to constructor
     * @return std::unique_ptr<T> instance
     */
    template<typename T, typename... Args>
    std::unique_ptr<T> make_unique(Args&&... args)
    {
        return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
    }
}
