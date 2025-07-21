
---
description: 'Guide to configuring ClickHouse with Azure Blob Storage backend'
sidebar_label: 'Configuring ClickHouse with Azure Blob Storage'
sidebar_position: 70
slug: /operations/configuring-clickhouse-with-azure-blob-storage
title: 'Configuring ClickHouse with Azure Blob Storage Backend'
---

# Configuring ClickHouse with Azure Blob Storage Backend

This guide provides comprehensive instructions for configuring ClickHouse in a Docker container with Azure Blob Storage as the backend. It covers both legacy and modern object storage configuration methods.

## Prerequisites

Before you begin, ensure you have:

1. **Docker** installed on your system
2. **Azure Storage Account** with Blob Storage enabled
3. **Azure credentials**: either connection string or account name/key
4. Basic understanding of ClickHouse concepts and Docker usage

## Option 1: Using Legacy Azure Blob Storage Disk Type

This is the traditional way to configure Azure Blob Storage in ClickHouse.

### Step 1: Create Azure Blob Storage Configuration File

Create a configuration file `azure_blob_storage.xml`:

```xml
<clickhouse>
    <storage_configuration>
        <disks>
            <azure_blob_storage_disk>
                <type>azure_blob_storage</type>
                <storage_account_url>https://your_account.blob.core.windows.net</storage_account_url>
                <container_name>your-container-name</container_name>
                <account_name>your_account_name</account_name>
                <account_key>your_account_key</account_key>
                <metadata_path>/var/lib/clickhouse/disks/azure_blob_storage_disk/</metadata_path>
                <skip_access_check>false</skip_access_check>
            </azure_blob_storage_disk>
        </disks>
        <policies>
            <azure_policy>
                <volumes>
                    <main><disk>azure_blob_storage_disk</disk></main>
                </volumes>
            </azure_policy>
        </policies>
    </storage_configuration>
</clickhouse>
```

### Step 2: Run ClickHouse Docker Container

```bash
docker run -d \
    --name clickhouse-server \
    -v "$PWD/azure_blob_storage.xml:/etc/clickhouse-server/config.d/azure_blob_storage.xml" \
    -e AZURE_ACCOUNT_NAME=your_account_name \
    -e AZURE_ACCOUNT_KEY=your_account_key \
    -p 8123:8123 -p 9000:9000 \
    clickhouse/clickhouse-server
```

## Option 2: Using Modern Object Storage Configuration (Recommended)

Starting from ClickHouse v24.1, a more flexible object storage configuration is available.

### Step 1: Create Object Storage Configuration File

Create a configuration file `azure_object_storage.xml`:

```xml
<clickhouse>
    <storage_configuration>
        <disks>
            <azure_disk>
                <type>object_storage</type>
                <object_storage_type>azure</object_storage_type>
                <metadata_type>local</metadata_type>
                <endpoint>https://your_account.blob.core.windows.net/your-container-name/</endpoint>
                <account_name>your_account_name</account_name>
                <account_key>your_account_key</account_key>
            </azure_disk>
        </disks>
        <policies>
            <azure_policy>
                <volumes>
                    <main><disk>azure_disk</disk></main>
                </volumes>
            </azure_policy>
        </policies>
    </storage_configuration>
</clickhouse>
```

### Step 2: Run ClickHouse Docker Container

```bash
docker run -d \
    --name clickhouse-server \
    -v "$PWD/azure_object_storage.xml:/etc/clickhouse-server/config.d/azure_object_storage.xml" \
    -e AZURE_ACCOUNT_NAME=your_account_name \
    -e AZURE_ACCOUNT_KEY=your_account_key \
    -p 8123:8123 -p 9000:9000 \
    clickhouse/clickhouse-server
```

## Option 3: Using Connection String for Authentication

For enhanced security, you can use a connection string instead of separate account name and key.

### Step 1: Create Connection String Configuration File

Create a configuration file `azure_connection_string.xml`:

```xml
<clickhouse>
    <storage_configuration>
        <disks>
            <azure_disk>
                <type>object_storage</type>
                <object_storage_type>azure</object_storage_type>
                <metadata_type>local</metadata_type>
                <endpoint>https://your_account.blob.core.windows.net/your-container-name/</endpoint>
                <connection_string from_env="AZURE_CONNECTION_STRING"/></connection_string>
            </azure_disk>
        </disks>
        <policies>
            <azure_policy>
                <volumes>
                    <main><disk>azure_disk</disk></main>
                </volumes>
            </azure_policy>
        </policies>
    </storage_configuration>
</clickhouse>
```

### Step 2: Run ClickHouse Docker Container

```bash
docker run -d \
    --name clickhouse-server \
    -v "$PWD/azure_connection_string.xml:/etc/clickhouse-server/config.d/azure_connection_string.xml" \
    -e AZURE_CONNECTION_STRING="DefaultEndpointsProtocol=https;AccountName=your_account_name;AccountKey=your_account_key;EndpointSuffix=core.windows.net" \
    -p 8123:8123 -p 9000:9000 \
    clickhouse/clickhouse-server
```

## Setting Azure Blob Storage as Default for All Tables

To make Azure Blob Storage the default storage backend for all MergeTree tables, add this configuration:

```xml
<clickhouse>
    <merge_tree>
        <storage_policy>azure_policy</storage_policy>
    </merge_tree>
</clickhouse>
```

## Creating Tables with Azure Storage

Once configured, you can create tables that use Azure Blob Storage:

### Using Legacy Configuration

```sql
CREATE TABLE azure_table (
    id UInt64,
    name String
) ENGINE = MergeTree()
ORDER BY id
SETTINGS storage_policy = 'azure_policy';
```

### Using Object Storage Configuration

```sql
CREATE TABLE azure_object_table (
    id UInt64,
    name String
) ENGINE = MergeTree()
ORDER BY id
SETTINGS disk = 'azure_disk';
```

## Environment Variables for Security

For better security, use environment variables to pass credentials:

- `AZURE_CONNECTION_STRING`: Full Azure connection string
- `AZURE_ACCOUNT_NAME`: Account name (if using separate credentials)
- `AZURE_ACCOUNT_KEY`: Account key (if using separate credentials)

## Troubleshooting

1. **Permission Issues**: Ensure your Azure account has proper permissions for the container
2. **Network Issues**: Verify Docker can reach Azure Blob Storage endpoints
3. **Configuration Errors**: Check ClickHouse logs for configuration parsing errors

## Cleanup

To stop and remove the ClickHouse container:

```bash
docker stop clickhouse-server
docker rm clickhouse-server
```

## Conclusion

This guide provides multiple approaches to configure ClickHouse with Azure Blob Storage backend in Docker containers. The modern object storage approach is recommended for new deployments as it offers more flexibility and better integration with future ClickHouse versions.
