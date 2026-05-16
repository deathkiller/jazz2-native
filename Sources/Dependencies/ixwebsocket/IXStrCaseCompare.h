/*
 *  IXStrCaseCompare.h
 *  Author: Benjamin Sergeant
 *  Copyright (c) 2020 Machine Zone. All rights reserved.
 */

#pragma once

#include <string>

namespace ix
{
    /**
    	@brief Case-insensitive string comparison functor for map/set keys.
    */
    struct CaseInsensitiveLess
    {
        /**
         * @brief Case-insensitive character comparison.
         */
        struct NocaseCompare
        {
            bool operator()(const unsigned char& c1, const unsigned char& c2) const;
        };

        /**
         * @brief Compare two strings case-insensitively.
         * @param s1 First string
         * @param s2 Second string
         * @return True if s1 < s2 (case-insensitive)
         */
        static bool cmp(const std::string& s1, const std::string& s2);

        /**
         * @brief Functor call for map/set key comparison.
         * @param s1 First string
         * @param s2 Second string
         * @return True if s1 < s2 (case-insensitive)
         */
        bool operator()(const std::string& s1, const std::string& s2) const;
    };
}
