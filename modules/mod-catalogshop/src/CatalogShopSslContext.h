/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef MOD_CATALOGSHOP_SSL_CONTEXT_H
#define MOD_CATALOGSHOP_SSL_CONTEXT_H

#include <boost/asio/ssl/context.hpp>
#include <string>

namespace ModCatalogShop
{
class SslContext
{
public:
    static bool Initialize(std::string const& certificateFile, std::string const& privateKeyFile);
    static boost::asio::ssl::context& instance();
};
}

#endif // MOD_CATALOGSHOP_SSL_CONTEXT_H
