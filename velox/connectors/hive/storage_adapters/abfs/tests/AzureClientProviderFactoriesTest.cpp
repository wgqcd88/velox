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

#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <string>

#include "velox/common/base/tests/GTestUtils.h"
#include "velox/connectors/hive/storage_adapters/abfs/AzureClientProviderFactories.h"
#include "velox/connectors/hive/storage_adapters/abfs/AzureClientProviderImpl.h"
#include "velox/connectors/hive/storage_adapters/abfs/RegisterAbfsFileSystem.h"

using namespace facebook::velox;
using namespace facebook::velox::filesystems;

namespace {

class DummyAzureClientProvider final : public AzureClientProvider {
 public:
  std::unique_ptr<AzureBlobClient> getReadFileClient(
      const std::shared_ptr<AbfsPath>& path,
      const config::ConfigBase& config) override {
    VELOX_FAIL("DummyAzureClientProvider: Not implemented.");
  }

  std::unique_ptr<AzureDataLakeFileClient> getWriteFileClient(
      const std::shared_ptr<AbfsPath>& path,
      const config::ConfigBase& config) override {
    VELOX_FAIL("DummyAzureClientProvider: Not implemented.");
  }
};

class WorkloadIdentityTestEnvironment {
 public:
  WorkloadIdentityTestEnvironment() {
    oldTenantId_ = getEnv("AZURE_TENANT_ID");
    oldClientId_ = getEnv("AZURE_CLIENT_ID");
    oldTokenFilePath_ = getEnv("AZURE_FEDERATED_TOKEN_FILE");
    tokenFilePath_ = testing::TempDir() + "azure-federated-token";
    std::ofstream(tokenFilePath_) << "token";
    setenv("AZURE_TENANT_ID", "00000000-0000-0000-0000-000000000000", 1);
    setenv("AZURE_CLIENT_ID", "11111111-1111-1111-1111-111111111111", 1);
    setenv("AZURE_FEDERATED_TOKEN_FILE", tokenFilePath_.c_str(), 1);
  }

  ~WorkloadIdentityTestEnvironment() {
    restoreEnv("AZURE_TENANT_ID", oldTenantId_);
    restoreEnv("AZURE_CLIENT_ID", oldClientId_);
    restoreEnv("AZURE_FEDERATED_TOKEN_FILE", oldTokenFilePath_);
    ::remove(tokenFilePath_.c_str());
  }

 private:
  static std::optional<std::string> getEnv(const char* name) {
    if (const auto* value = std::getenv(name)) {
      return value;
    }
    return std::nullopt;
  }

  static void restoreEnv(
      const char* name,
      const std::optional<std::string>& value) {
    if (value.has_value()) {
      setenv(name, value->c_str(), 1);
    } else {
      unsetenv(name);
    }
  }

  std::string tokenFilePath_;
  std::optional<std::string> oldTenantId_;
  std::optional<std::string> oldClientId_;
  std::optional<std::string> oldTokenFilePath_;
};

} // namespace

TEST(AzureClientProviderFactoriesTest, registerFromConfig) {
  const auto abfsPath = std::make_shared<AbfsPath>(
      "abfss://abc@efg.dfs.core.windows.net/file/test.txt");

  {
    // OAuth auth type.
    const config::ConfigBase config(
        {{"fs.azure.account.auth.type.efg.dfs.core.windows.net", "OAuth"},
         {"fs.azure.account.oauth2.client.id.efg.dfs.core.windows.net", "123"},
         {"fs.azure.account.oauth2.client.secret.efg.dfs.core.windows.net",
          "456"},
         {"fs.azure.account.oauth2.client.endpoint.efg.dfs.core.windows.net",
          "https://login.microsoftonline.com/{TENANTID}/oauth2/token"}},
        false);
    registerAzureClientProvider(config);

    ASSERT_NE(
        AzureClientProviderFactories::getReadFileClient(abfsPath, config),
        nullptr);
    ASSERT_NE(
        AzureClientProviderFactories::getWriteFileClient(abfsPath, config),
        nullptr);
  }

  {
    // SharedKey auth type.
    const config::ConfigBase config(
        {{"fs.azure.account.auth.type.efg.dfs.core.windows.net", "SharedKey"},
         {"fs.azure.account.key.efg.dfs.core.windows.net", "456"}},
        false);
    registerAzureClientProvider(config);

    ASSERT_NE(
        AzureClientProviderFactories::getReadFileClient(abfsPath, config),
        nullptr);
    ASSERT_NE(
        AzureClientProviderFactories::getWriteFileClient(abfsPath, config),
        nullptr);
  }

  {
    // SAS auth type.
    const config::ConfigBase config(
        {{"fs.azure.account.auth.type.efg.dfs.core.windows.net", "SAS"},
         {"fs.azure.sas.fixed.token.efg.dfs.core.windows.net", "456"}},
        false);
    registerAzureClientProvider(config);

    ASSERT_NE(
        AzureClientProviderFactories::getReadFileClient(abfsPath, config),
        nullptr);
    ASSERT_NE(
        AzureClientProviderFactories::getWriteFileClient(abfsPath, config),
        nullptr);
  }

  {
    // Workload Identity using Hadoop's OAuth token provider configuration.
    WorkloadIdentityTestEnvironment workloadIdentityEnvironment;
    const config::ConfigBase config(
        {{"fs.azure.account.auth.type.efg.dfs.core.windows.net", "OAuth"},
         {"fs.azure.account.oauth.provider.type.efg.dfs.core.windows.net",
          "org.apache.hadoop.fs.azurebfs.oauth2."
          "WorkloadIdentityTokenProvider"}},
        false);
    registerAzureClientProvider(config);

    ASSERT_NE(
        AzureClientProviderFactories::getReadFileClient(abfsPath, config),
        nullptr);
    ASSERT_NE(
        AzureClientProviderFactories::getWriteFileClient(abfsPath, config),
        nullptr);
  }

  {
    // Legacy WorkloadIdentity auth type.
    WorkloadIdentityTestEnvironment workloadIdentityEnvironment;
    const config::ConfigBase config(
        {{"fs.azure.account.auth.type.efg.dfs.core.windows.net",
          "WorkloadIdentity"}},
        false);
    registerAzureClientProvider(config);

    ASSERT_NE(
        AzureClientProviderFactories::getReadFileClient(abfsPath, config),
        nullptr);
    ASSERT_NE(
        AzureClientProviderFactories::getWriteFileClient(abfsPath, config),
        nullptr);
  }

  {
    // Account-specific auth takes precedence over global auth, while other
    // accounts use the global configuration.
    WorkloadIdentityTestEnvironment workloadIdentityEnvironment;
    const config::ConfigBase config(
        {{"fs.azure.account.auth.type", "OAuth"},
         {"fs.azure.account.oauth.provider.type",
          "org.apache.hadoop.fs.azurebfs.oauth2."
          "WorkloadIdentityTokenProvider"},
         {"fs.azure.account.auth.type.efg.dfs.core.windows.net", "SAS"},
         {"fs.azure.sas.fixed.token.efg.dfs.core.windows.net", "sas=test"}},
        false);
    registerAzureClientProvider(config);

    EXPECT_EQ(
        AzureClientProviderFactories::getReadFileClient(abfsPath, config)
            ->getUrl(),
        "https://efg.blob.core.windows.net/abc/file/test.txt?sas=test");

    const auto defaultAuthPath = std::make_shared<AbfsPath>(
        "abfss://abc@other.dfs.core.windows.net/file/test.txt");
    EXPECT_EQ(
        AzureClientProviderFactories::getReadFileClient(defaultAuthPath, config)
            ->getUrl(),
        "https://other.blob.core.windows.net/abc/file/test.txt");
    registerAzureClientProvider(config::ConfigBase({}));
  }

  {
    // Invalid auth type.
    const config::ConfigBase config(
        {{"fs.azure.account.auth.type.efg.dfs.core.windows.net", "Custom"},
         {"fs.azure.account.key.efg.dfs.core.windows.net", "456"}},
        false);
    VELOX_ASSERT_THROW(
        registerAzureClientProvider(config),
        "Unsupported auth type Custom, supported auth types are SharedKey, OAuth, SAS and WorkloadIdentity.");
  }

  {
    // Invalid config key.
    const config::ConfigBase config(
        {{"fs.azure.account.auth.type.efg", "SharedKey"},
         {"fs.azure.account.key.efg.dfs.core.windows.net", "456"}},
        false);
    VELOX_ASSERT_THROW(
        registerAzureClientProvider(config),
        "Invalid Azure account auth type key: fs.azure.account.auth.type.efg");
  }
}

TEST(AzureClientProviderFactoriesTest, registerCustomFactory) {
  static const std::string path =
      "abfs://test@custom.dfs.core.windows.net/test";
  const auto abfsPath = std::make_shared<AbfsPath>(path);

  registerAzureClientProviderFactory(
      "custom",
      [](const std::string& account) -> std::unique_ptr<AzureClientProvider> {
        return std::make_unique<DummyAzureClientProvider>();
      });

  ASSERT_NO_THROW(AzureClientProviderFactories::getClientFactory("custom"));
  VELOX_ASSERT_THROW(
      AzureClientProviderFactories::getReadFileClient(
          abfsPath, config::ConfigBase({})),
      "DummyAzureClientProvider: Not implemented.");
  VELOX_ASSERT_THROW(
      AzureClientProviderFactories::getWriteFileClient(
          abfsPath, config::ConfigBase({})),
      "DummyAzureClientProvider: Not implemented.");

  VELOX_ASSERT_THROW(
      AzureClientProviderFactories::getClientFactory("unregistered"),
      "No AzureClientProviderFactory registered for account 'unregistered' and "
      "no default factory is registered.");
}
