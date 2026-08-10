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

#include <folly/String.h>

#include <optional>
#include <unordered_map>

#include "velox/common/config/Config.h"
#include "velox/connectors/hive/storage_adapters/abfs/AbfsPath.h"

namespace facebook::velox::filesystems {

namespace {

std::string resolveAuthType(
    const config::ConfigBase& config,
    const std::string& authType,
    std::optional<std::string_view> accountNameWithSuffix) {
  auto normalizedAuthType = authType;
  folly::toLowerAscii(normalizedAuthType);
  if (normalizedAuthType == "wi") {
    return kAzureWorkloadIdentityAuthType;
  }
  if (normalizedAuthType == "mi" ||
      authType == kAzureMsiTokenProvider) {
    return kAzureManagedIdentityAuthType;
  }
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
  if (providerType.has_value() &&
      providerType.value() == kAzureMsiTokenProvider) {
    return kAzureManagedIdentityAuthType;
  }
  return authType;
}

} // namespace

std::vector<CacheKey> extractCacheKeyFromConfig(
    const config::ConfigBase& config) {
  std::unordered_map<std::string, std::string> accountAuthTypes;
  const auto veloxGlobalAuthType =
      config.get<std::string>(kVeloxAzureAuthType);

  const auto addAccountAuthTypes = [&](const char* authTypeKey, bool overwrite) {
    const auto accountAuthTypePrefix = fmt::format("{}.", authTypeKey);
    for (const auto& [key, value] : config.rawConfigs()) {
      if (key.find(accountAuthTypePrefix) != 0) {
        continue;
      }
      const auto remaining =
          std::string_view(key).substr(accountAuthTypePrefix.size());
      const auto dot = remaining.find(".");
      VELOX_USER_CHECK_NE(
          dot,
          std::string_view::npos,
          "Invalid Azure account auth type key: {}",
          key);
      const auto accountName = std::string(remaining.substr(0, dot));
      const auto resolvedAuthType = resolveAuthType(config, value, remaining);
      if (overwrite) {
        accountAuthTypes.insert_or_assign(accountName, resolvedAuthType);
      } else {
        accountAuthTypes.emplace(accountName, resolvedAuthType);
      }
    }
  };

  // A global Velox setting supersedes all Hadoop account-specific settings.
  if (!veloxGlobalAuthType.has_value()) {
    addAccountAuthTypes(kAzureAccountAuthType, false);
  }
  addAccountAuthTypes(kVeloxAzureAuthType, true);

  std::vector<CacheKey> cacheKeys;
  cacheKeys.reserve(accountAuthTypes.size() + 1);
  for (const auto& [accountName, authType] : accountAuthTypes) {
    cacheKeys.emplace_back(accountName, authType);
  }

  if (veloxGlobalAuthType.has_value()) {
    cacheKeys.emplace_back(
        "",
        resolveAuthType(config, veloxGlobalAuthType.value(), std::nullopt));
  } else if (const auto globalAuthType =
                 config.get<std::string>(kAzureAccountAuthType)) {
    cacheKeys.emplace_back(
        "",
        resolveAuthType(config, globalAuthType.value(), std::nullopt));
  }
  return cacheKeys;
}

} // namespace facebook::velox::filesystems
