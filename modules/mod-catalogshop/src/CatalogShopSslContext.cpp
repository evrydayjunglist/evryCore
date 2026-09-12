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

#include "CatalogShopSslContext.h"
#include "Log.h"

bool ModCatalogShop::SslContext::Initialize(std::string const& certificateFile, std::string const& privateKeyFile)
{
    boost::system::error_code err;
    instance().set_options(boost::asio::ssl::context::default_workarounds
        | boost::asio::ssl::context::no_sslv2
        | boost::asio::ssl::context::no_sslv3
        | boost::asio::ssl::context::single_dh_use, err);
    if (err)
    {
        TC_LOG_ERROR("module.catalogshop", "SSL set_options failed: {}", err.message());
        return false;
    }

    instance().use_certificate_chain_file(certificateFile, err);
    if (err)
    {
        TC_LOG_ERROR("module.catalogshop", "Failed to load CatalogShop certificate '{}': {}", certificateFile, err.message());
        return false;
    }

    instance().use_private_key_file(privateKeyFile, boost::asio::ssl::context::pem, err);
    if (err)
    {
        TC_LOG_ERROR("module.catalogshop", "Failed to load CatalogShop private key '{}': {}", privateKeyFile, err.message());
        return false;
    }

    TC_LOG_INFO("module.catalogshop", "Loaded CatalogShop TLS cert='{}' key='{}'", certificateFile, privateKeyFile);
    return true;
}

boost::asio::ssl::context& ModCatalogShop::SslContext::instance()
{
    static boost::asio::ssl::context context(boost::asio::ssl::context::tls_server);
    return context;
}
