/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "velox/connectors/hive/storage_adapters/abfs/AbfsUtil.h"

#include <optional>

#include "velox/common/config/Config.h"
#include "velox/connectors/hive/storage_adapters/abfs/AbfsPath.h"

namespace facebook::velox::filesystems {

namespace {

std::string resolveAuthType(
    const config::ConfigBase& config,
    const std::string& authType,
    std::optional<std::string_view> accountNameWithSuffix) {
  if (authType != kAzureOAuthAuthType) {
    return authType;
  }

  std::optional<std::string> providerType;
  if (accountNameWithSuffix.has_value()) {
    const auto providerKey = fmt::format(
        "{}.{}", kAzureAccountOAuthProviderType, accountNameWithSuffix.value());
    providerType = config.get<std::string>(providerKey);
  }
  if (!providerType.has_value()) {
    providerType =
        config.get<std::string>(kAzureAccountOAuthProviderType);
  }
  if (providerType.has_value() &&
      providerType.value() == kAzureWorkloadIdentityTokenProvider) {
    return kAzureWorkloadIdentityAuthType;
  }
  return authType;
}

} // namespace

std::vector<CacheKey> extractCacheKeyFromConfig(
    const config::ConfigBase& config) {
  std::vector<CacheKey> cacheKeys;
  constexpr std::string_view authTypePrefix{kAzureAccountAuthType};
  const auto accountAuthTypePrefix = fmt::format("{}.", authTypePrefix);
  for (const auto& [key, value] : config.rawConfigs()) {
    if (key.find(accountAuthTypePrefix) != 0) {
      continue;
    }
    // Extract the accountName after "fs.azure.account.auth.type.".
    auto remaining =
        std::string_view(key).substr(accountAuthTypePrefix.size());
    auto dot = remaining.find(".");
    VELOX_USER_CHECK_NE(
        dot,
        std::string_view::npos,
        "Invalid Azure account auth type key: {}",
        key);
    cacheKeys.emplace_back(CacheKey{
        remaining.substr(0, dot),
        resolveAuthType(config, value, remaining)});
  }

  if (const auto globalAuthType =
          config.get<std::string>(kAzureAccountAuthType)) {
    cacheKeys.emplace_back(
        CacheKey{
            "",
            resolveAuthType(config, globalAuthType.value(), std::nullopt)});
  }
  return cacheKeys;
}

} // namespace facebook::velox::filesystems
