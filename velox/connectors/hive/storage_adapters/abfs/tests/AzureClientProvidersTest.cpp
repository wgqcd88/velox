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

#include "velox/connectors/hive/storage_adapters/abfs/AzureClientProviderImpl.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <string>

#include "connectors/hive/storage_adapters/abfs/RegisterAbfsFileSystem.h"
#include "velox/common/base/tests/GTestUtils.h"
#include "velox/common/config/Config.h"
#include "velox/connectors/hive/storage_adapters/abfs/AbfsPath.h"

#include "gtest/gtest.h"

using namespace facebook::velox::filesystems;
using namespace facebook::velox;

namespace {

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

TEST(AzureClientProvidersTest, clientSecretOAuth) {
  const config::ConfigBase config(
      {{"fs.azure.account.oauth2.client.id.efg.dfs.core.windows.net", "test"},
       {"fs.azure.account.oauth2.client.secret.efg.dfs.core.windows.net",
        "test"},
       {"fs.azure.account.oauth2.client.endpoint.efg.dfs.core.windows.net",
        "https://login.microsoftonline.com/{TENANTID}/oauth2/token"},
       {"fs.azure.account.oauth2.client.id.bar2.dfs.core.windows.net", "test"},
       {"fs.azure.account.oauth2.client.id.bar3.dfs.core.windows.net", "test"},
       {"fs.azure.account.oauth2.client.secret.bar3.dfs.core.windows.net",
        "test"}},
      false);

  auto clientProvider = OAuthAzureClientProvider();

  VELOX_ASSERT_USER_THROW(
      clientProvider.tenantIdAndAuthorityHost(
          std::make_shared<AbfsPath>(
              "abfss://foo@bar1.dfs.core.windows.net/test.txt"),
          config),
      "Config fs.azure.account.oauth2.client.id.bar1.dfs.core.windows.net not found");
  VELOX_ASSERT_USER_THROW(
      clientProvider.tenantIdAndAuthorityHost(
          std::make_shared<AbfsPath>(
              "abfss://foo@bar2.dfs.core.windows.net/test.txt"),
          config),
      "Config fs.azure.account.oauth2.client.secret.bar2.dfs.core.windows.net not found");
  VELOX_ASSERT_USER_THROW(
      clientProvider.tenantIdAndAuthorityHost(
          std::make_shared<AbfsPath>(
              "abfss://foo@bar3.dfs.core.windows.net/test.txt"),
          config),
      "Config fs.azure.account.oauth2.client.endpoint.bar3.dfs.core.windows.net not found");

  const auto expectedTenantIdAndAuthorityHost =
      std::make_pair<std::string, std::string>(
          "{TENANTID}", "https://login.microsoftonline.com/");
  EXPECT_EQ(
      clientProvider.tenantIdAndAuthorityHost(
          std::make_shared<AbfsPath>(
              "abfss://abc@efg.dfs.core.windows.net/file/test.txt"),
          config),
      expectedTenantIdAndAuthorityHost);

  const auto abfsPath = std::make_shared<AbfsPath>(
      "abfss://abc@efg.dfs.core.windows.net/file/test.txt");
  auto readClient = clientProvider.getReadFileClient(abfsPath, config);
  EXPECT_EQ(
      readClient->getUrl(),
      "https://efg.blob.core.windows.net/abc/file/test.txt");
  auto writeClient = clientProvider.getWriteFileClient(abfsPath, config);
  // GetUrl retrieves the value from the internal blob client, which represents
  // the blob's path as well.
  EXPECT_EQ(
      writeClient->getUrl(),
      "https://efg.blob.core.windows.net/abc/file/test.txt");
}

TEST(AzureClientProviderTest, fixedSasToken) {
  const config::ConfigBase config(
      {{"fs.azure.sas.fixed.token.bar.dfs.core.windows.net", "sas=test"}},
      false);

  auto clientProvider = FixedSasAzureClientProvider();

  VELOX_ASSERT_USER_THROW(
      clientProvider.sas(
          std::make_shared<AbfsPath>(
              "abfss://foo@efg.dfs.core.windows.net/test.txt"),
          config),
      "Config fs.azure.sas.fixed.token.efg.dfs.core.windows.net not found");

  const auto abfsPath =
      std::make_shared<AbfsPath>("abfs://abc@bar.dfs.core.windows.net/file");
  auto readClient = clientProvider.getReadFileClient(abfsPath, config);
  EXPECT_EQ(
      readClient->getUrl(),
      "http://bar.blob.core.windows.net/abc/file?sas=test");
  auto writeClient = clientProvider.getWriteFileClient(abfsPath, config);
  // GetUrl retrieves the value from the internal blob client, which represents
  // the blob's path as well.
  EXPECT_EQ(
      writeClient->getUrl(),
      "http://bar.blob.core.windows.net/abc/file?sas=test");
}

TEST(AzureClientProviderTest, workloadIdentity) {
  WorkloadIdentityTestEnvironment workloadIdentityEnvironment;
  auto clientProvider = WorkloadIdentityAzureClientProvider();
  const auto abfsPath =
      std::make_shared<AbfsPath>("abfss://abc@bar.dfs.core.windows.net/file");
  const auto config = config::ConfigBase({}, false);

  auto readClient = clientProvider.getReadFileClient(abfsPath, config);
  EXPECT_EQ(readClient->getUrl(), "https://bar.blob.core.windows.net/abc/file");

  auto writeClient = clientProvider.getWriteFileClient(abfsPath, config);
  EXPECT_EQ(writeClient->getUrl(), "https://bar.blob.core.windows.net/abc/file");
}

TEST(AzureClientProviderTest, managedIdentity) {
  const config::ConfigBase config(
      {{"fs.azure.account.oauth2.msi.tenant", "global-tenant"},
       {"fs.azure.account.oauth2.client.id", "global-client"},
       {"fs.azure.account.oauth2.msi.tenant.bar.dfs.core.windows.net",
        "account-tenant"},
       {"fs.azure.account.oauth2.client.id.bar.dfs.core.windows.net",
        "account-client"}},
      false);
  auto clientProvider = ManagedIdentityAzureClientProvider();

  const auto accountPath =
      std::make_shared<AbfsPath>("abfss://abc@bar.dfs.core.windows.net/file");
  EXPECT_EQ(
      clientProvider.tenantIdAndClientId(accountPath, config),
      std::make_pair<std::string, std::string>(
          "account-tenant", "account-client"));
  EXPECT_EQ(
      clientProvider.getReadFileClient(accountPath, config)->getUrl(),
      "https://bar.blob.core.windows.net/abc/file");
  EXPECT_EQ(
      clientProvider.getWriteFileClient(accountPath, config)->getUrl(),
      "https://bar.blob.core.windows.net/abc/file");

  const auto globalPath =
      std::make_shared<AbfsPath>("abfss://abc@foo.dfs.core.windows.net/file");
  EXPECT_EQ(
      clientProvider.tenantIdAndClientId(globalPath, config),
      std::make_pair<std::string, std::string>(
          "global-tenant", "global-client"));
}

TEST(AzureClientProviderTest, sharedKey) {
  const config::ConfigBase config(
      {{"fs.azure.account.key.efg.dfs.core.windows.net", "123"},
       {"fs.azure.account.key.foobar.dfs.core.windows.net", "456"},
       {"fs.azure.account.key.bar.dfs.core.windows.net", "789"}},
      false);

  const auto abfsPath =
      std::make_shared<AbfsPath>("abfs://abc@efg.dfs.core.windows.net/file");
  EXPECT_EQ(abfsPath->fileSystem(), "abc");
  EXPECT_EQ(abfsPath->filePath(), "file");

  auto clientProvider = SharedKeyAzureClientProvider();
  EXPECT_EQ(
      clientProvider.connectionString(abfsPath, config),
      "DefaultEndpointsProtocol=http;AccountName=efg;AccountKey=123;EndpointSuffix=core.windows.net;");

  const auto abfssPath = std::make_shared<AbfsPath>(
      "abfss://abc@foobar.dfs.core.windows.net/sf_1/store_sales/ss_sold_date_sk=2450816/part-00002-a29c25f1-4638-494e-8428-a84f51dcea41.c000.snappy.parquet");
  EXPECT_EQ(abfssPath->fileSystem(), "abc");
  EXPECT_EQ(
      abfssPath->filePath(),
      "sf_1/store_sales/ss_sold_date_sk=2450816/part-00002-a29c25f1-4638-494e-8428-a84f51dcea41.c000.snappy.parquet");
  EXPECT_EQ(
      clientProvider.connectionString(abfssPath, config),
      "DefaultEndpointsProtocol=https;AccountName=foobar;AccountKey=456;EndpointSuffix=core.windows.net;");

  // Test with special character space.
  const auto abfssPathWithSpecialCharacters = std::make_shared<AbfsPath>(
      "abfss://foo@bar.dfs.core.windows.net/main@dir/sub dir/test.txt");
  EXPECT_EQ(abfssPathWithSpecialCharacters->fileSystem(), "foo");
  EXPECT_EQ(
      abfssPathWithSpecialCharacters->filePath(), "main@dir/sub dir/test.txt");
  EXPECT_EQ(
      clientProvider.connectionString(abfssPathWithSpecialCharacters, config),
      "DefaultEndpointsProtocol=https;AccountName=bar;AccountKey=789;EndpointSuffix=core.windows.net;");

  VELOX_ASSERT_USER_THROW(
      clientProvider.connectionString(
          std::make_shared<AbfsPath>(
              "abfss://foo@otheraccount.dfs.core.windows.net/test.txt"),
          config),
      "Config fs.azure.account.key.otheraccount.dfs.core.windows.net not found");
}
