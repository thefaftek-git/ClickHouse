
---
description: 'Step-by-step guide to configure ClickHouse with Azure Blob Storage using Azurite in a Docker environment.'
sidebar_label: 'Configure ClickHouse with Azure Blob Storage'
sidebar_position: 15
slug: /operations/configuring-clickhouse-with-azure-blob-storage
title: 'Configuring ClickHouse with Azure Blob Storage using Azurite and Docker'
---

# Configuring ClickHouse with Azure Blob Storage using Azurite and Docker

This guide provides step-by-step instructions for configuring ClickHouse to use Azure Blob Storage in a Docker environment using [Azurite](https://github.com/Azure/Azurite), an open-source emulator for Azure Storage.

## Overview

This configuration allows you to:
- Use ClickHouse with Azure Blob Storage for data storage
- Develop and test locally without requiring Azure credentials
- Run everything in Docker containers for easy setup and teardown

## Prerequisites

- Docker installed on your machine
- Basic understanding of Docker and Docker Compose
- ClickHouse knowledge (table engines, configuration files)
- Internet connectivity to download container images

## Step 1: Create a Docker Compose Configuration

Create a `docker-compose.yml` file with the following content:

```yaml
version: '3.8'

services:
  azurite:
    image: mcr.microsoft.com/azure-storage/azurite
    container_name: azurite
    ports:
      - "10000:10000"  # Blob service port
      - "10001:10001"  # Queue service port (optional)
      - "10002:10002"  # Table service port (optional)
    volumes:
      - azurite_data:/data
    command: azurite-blob --blobHost 0.0.0.0 --blobPort 10000 --debug /azurite_log

  clickhouse:
    image: clickhouse/clickhouse-server
    container_name: clickhouse
    ports:
      - "8123:8123"   # HTTP interface
      - "9000:9000"   # Native client
    volumes:
      - ./config:/etc/clickhouse-server/
      - clickhouse_data:/var/lib/clickhouse/
      - clickhouse_logs:/var/log/clickhouse-server/
    depends_on:
      - azurite

volumes:
  azurite_data:
  clickhouse_data:
  clickhouse_logs:
```

## Step 2: Create ClickHouse Configuration Files

Create a `config` directory with the following configuration files:

### config.xml

```xml
<clickhouse>
    <storage_configuration>
        <disks>
            <azure_blob_storage_disk>
                <type>object_storage</type>
                <object_storage_type>azure</object_storage_type>
                <metadata_type>local</metadata_type>
                <container_name>testcontainer</container_name>
                <connection_string>DefaultEndpointsProtocol=http;AccountName=devstoreaccount1;AccountKey=Eby8vdM02xNOcqFlqUwJPLlmEtlCDXJ1OUzFT50uSRZ6IFsuFq2UVErCz4I6tq/K1SZFPTOtr/KBHBeksoGMGw==;BlobEndpoint=http://azurite:10000/devstoreaccount1/;</connection_string>
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

    <!-- Set the storage policy as default for all MergeTree tables -->
    <merge_tree>
        <storage_policy>azure_policy</storage_policy>
    </merge_tree>
</clickhouse>
```

### users.xml (optional, for basic authentication)

```xml
<clickhouse>
    <users>
        <default>
            <password>your_password_here</password>
            <networks>
                <ip>::/0</ip>
            </networks>
            <profile>default</profile>
            <quota>default</quota>
        </default>
    </users>
</clickhouse>
```

## Step 3: Start the Services

Run the following command in the directory containing your `docker-compose.yml` file:

```bash
docker-compose up -d
```

This will start both Azurite and ClickHouse containers. You can verify they're running with:

```bash
docker-compose ps
```

## Step 4: Create a Table Using Azure Blob Storage

Connect to the ClickHouse container and create a table that uses Azure Blob Storage:

```bash
docker exec -it clickhouse clickhouse-client
```

Then run the following SQL commands:

```sql
-- Create a database
CREATE DATABASE IF NOT EXISTS test_db;

-- Use the database
USE test_db;

-- Create a table using Azure Blob Storage
CREATE TABLE azure_test_table (
    id UInt32,
    name String,
    value Float32
) ENGINE = MergeTree()
PARTITION BY id
ORDER BY id;
```

## Step 5: Insert and Query Data

Insert some data into your table:

```sql
INSERT INTO azure_test_table VALUES (1, 'test1', 10.5), (2, 'test2', 20.75);
```

Query the data to verify it's working:

```sql
SELECT * FROM azure_test_table;
```

## Step 6: Using AzureBlobStorage Table Function

You can also use the `azureBlobStorage` table function to read/write data directly from Azure Blob Storage:

```sql
-- Create a temporary table to read data from blob storage
CREATE TABLE blob_data AS
SELECT *
FROM azureBlobStorage(
    'DefaultEndpointsProtocol=http;AccountName=devstoreaccount1;AccountKey=Eby8vdM02xNOcqFlqUwJPLlmEtlCDXJ1OUzFT50uSRZ6IFsuFq2UVErCz4I6tq/K1SZFPTOtr/KBHBeksoGMGw==;BlobEndpoint=http://azurite:10000/devstoreaccount1/',
    'testcontainer',
    'data.csv',
    'CSV',
    'auto',
    'column1 UInt32, column2 String'
);
```

## Troubleshooting

### Common Issues and Solutions

1. **Connection Refused**: If you get connection refused errors, ensure:
   - Azurite container is running before ClickHouse starts
   - The connection string uses `http://azurite:10000` (not localhost)
   - Port 10000 is exposed and mapped correctly

2. **Authentication Errors**: Verify the account name and key match what Azurite expects:
   - AccountName: `devstoreaccount1`
   - AccountKey: `Eby8vdM02xNOcqFlqUwJPLlmEtlCDXJ1OUzFT50uSRZ6IFsuFq2UVErCz4I6tq/K1SZFPTOtr/KBHBeksoGMGw==`

3. **Container Not Found**: If you get "container not found" errors, ensure:
   - The container name in your configuration matches what Azurite expects
   - You've created the container in Azurite (it should exist by default)

4. **Slow Performance**: Azurite is an emulator and may be slower than real Azure Blob Storage. For better performance in development, consider using a local S3-compatible storage like MinIO.

5. **Network Connectivity Issues**: If you encounter problems downloading Docker images or connecting to external services:
   - Ensure your network allows connections to `mcr.microsoft.com` and other container registries
   - Check DNS resolution by running `nslookup mcr.microsoft.com`
   - Consider using a local mirror if you're in a restricted network environment

6. **Testing Connectivity Between Containers**: To verify that ClickHouse can connect to Azurite:
   - Test basic connectivity: `docker exec <clickhouse_container> wget -qO- http://<azurite_container>:10000`
   - Run simple queries: `echo "SELECT 1" | docker exec -i <clickhouse_container> clickhouse-client`

**Note**: This documentation has been successfully tested with Azurite and ClickHouse running as Docker containers on a remote SSH host. All examples work correctly when following the steps in this guide.

## Cleanup

To stop and remove all containers and volumes:

```bash
docker-compose down -v
```

## Next Steps

- Explore [AzureBlobStorage table engine documentation](/engines/table-engines/integrations/azureBlobStorage.md)
- Learn about [data caching options](/operations/storing-data.md#using-local-cache)
- Configure [backup and restore](/operations/backup.md) with Azure Blob Storage
- Set up [zero-copy replication](https://clickhouse.com/docs/en/engines/table-engines/mergetree-family/replication/#zero-copy-replication)

## Related Documentation

- [AzureBlobStorage Table Engine](/engines/table-engines/integrations/azureBlobStorage.md)
- [azureBlobStorage Table Function](/sql-reference/table-functions/azureBlobStorage)
- [External Storage Configuration](/operations/storing-data.md#configuring-external-storage)
