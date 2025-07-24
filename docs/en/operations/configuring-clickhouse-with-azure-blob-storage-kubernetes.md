

---
description: 'Step-by-step guide to configure ClickHouse with Azure Blob Storage using Azurite in a Kubernetes environment.'
sidebar_label: 'Configure ClickHouse with Azure Blob Storage (Kubernetes)'
sidebar_position: 16
slug: /operations/configuring-clickhouse-with-azure-blob-storage-kubernetes
title: 'Configuring ClickHouse with Azure Blob Storage using Azurite and Kubernetes'
---

# Configuring ClickHouse with Azure Blob Storage using Azurite and Kubernetes

This guide provides step-by-step instructions for configuring ClickHouse to use Azure Blob Storage in a Kubernetes environment using [Azurite](https://github.com/Azure/Azurite), an open-source emulator for Azure Storage.

## Overview

This configuration allows you to:
- Use ClickHouse with Azure Blob Storage for data storage
- Develop and test locally without requiring Azure credentials
- Run everything as Kubernetes pods for easy orchestration and management

## Prerequisites

- Kubernetes cluster (local or cloud-based)
- `kubectl` command-line tool configured to access your cluster
- Basic understanding of Kubernetes concepts (Pods, Services, PersistentVolumes)
- ClickHouse knowledge (table engines, configuration files)
- Internet connectivity to download container images

## Step 1: Create Kubernetes Manifests for Azurite and ClickHouse

Create a directory for your Kubernetes manifests:

```bash
mkdir clickhouse-azurite-k8s
cd clickhouse-azurite-k8s
```

### azurite-deployment.yaml

Create a deployment for the Azurite service:

```yaml
apiVersion: apps/v1
kind: Deployment
metadata:
  name: azurite
spec:
  replicas: 1
  selector:
    matchLabels:
      app: azurite
  template:
    metadata:
      labels:
        app: azurite
    spec:
      containers:
      - name: azurite
        image: mcr.microsoft.com/azure-storage/azurite
        ports:
        - containerPort: 10000
          name: blob-port
        command: ["azurite-blob", "--blobHost", "0.0.0.0", "--blobPort", "10000", "--debug", "/azurite_log"]
```

### azurite-service.yaml

Create a service to expose the Azurite deployment:

```yaml
apiVersion: v1
kind: Service
metadata:
  name: azurite
spec:
  selector:
    app: azurite
  ports:
    - protocol: TCP
      port: 10000
      targetPort: blob-port
```

### clickhouse-configmap.yaml

Create a ConfigMap for ClickHouse configuration:

```yaml
apiVersion: v1
kind: ConfigMap
metadata:
  name: clickhouse-config
data:
  config.xml: |
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

### clickhouse-deployment.yaml

Create a deployment for ClickHouse:

```yaml
apiVersion: apps/v1
kind: Deployment
metadata:
  name: clickhouse
spec:
  replicas: 1
  selector:
    matchLabels:
      app: clickhouse
  template:
    metadata:
      labels:
        app: clickhouse
    spec:
      containers:
      - name: clickhouse
        image: clickhouse/clickhouse-server
        ports:
        - containerPort: 8123
          name: http-port
        - containerPort: 9000
          name: native-port
        volumeMounts:
        - name: config-volume
          mountPath: /etc/clickhouse-server/
          subPath: config.xml
      volumes:
      - name: config-volume
        configMap:
          name: clickhouse-config
```

### clickhouse-service.yaml

Create a service to expose ClickHouse:

```yaml
apiVersion: v1
kind: Service
metadata:
  name: clickhouse
spec:
  selector:
    app: clickhouse
  ports:
    - protocol: TCP
      port: 8123
      targetPort: http-port
      name: http
    - protocol: TCP
      port: 9000
      targetPort: native-port
      name: native
```

## Step 2: Deploy to Kubernetes

Apply the manifests to your Kubernetes cluster:

```bash
kubectl apply -f azurite-deployment.yaml
kubectl apply -f azurite-service.yaml
kubectl apply -f clickhouse-configmap.yaml
kubectl apply -f clickhouse-deployment.yaml
kubectl apply -f clickhouse-service.yaml
```

Verify that the pods are running:

```bash
kubectl get pods
```

You should see both `azurite` and `clickhouse` pods in the running state.

## Step 3: Create a Table Using Azure Blob Storage

Get a shell into the ClickHouse pod:

```bash
kubectl exec -it $(kubectl get pods -l app=clickhouse -o jsonpath='{.items[0].metadata.name}') -- clickhouse-client
```

Then run the following SQL commands to create and use a database, and create a table with Azure Blob Storage:

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

## Step 4: Insert and Query Data

Insert some data into your table:

```sql
INSERT INTO azure_test_table VALUES (1, 'test1', 10.5), (2, 'test2', 20.75);
```

Query the data to verify it's working:

```sql
SELECT * FROM azure_test_table;
```

## Step 5: Using AzureBlobStorage Table Function

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

1. **Connection Refused**: If you get connection refused errors:
   - Ensure Azurite pod is running before ClickHouse starts
   - Verify the connection string uses `http://azurite:10000` (Kubernetes service name)
   - Check that port 10000 is exposed correctly in the service

2. **Authentication Errors**: Verify the account name and key match what Azurite expects:
   - AccountName: `devstoreaccount1`
   - AccountKey: `Eby8vdM02xNOcqFlqUwJPLlmEtlCDXJ1OUzFT50uSRZ6IFsuFq2UVErCz4I6tq/K1SZFPTOtr/KBHBeksoGMGw==`

3. **Container Not Found**: If you get "container not found" errors:
   - Ensure the container name in your configuration matches what Azurite expects
   - Verify that Azurite is properly initialized and the default container exists

4. **Pod Communication Issues**:
   - Test connectivity between pods: `kubectl exec <clickhouse_pod> -- wget -qO- http://azurite:10000`
   - Check service DNS resolution by running `nslookup azurite` from within a pod

5. **Persistent Storage**: If you need persistent storage for Azurite data:
   - Add PersistentVolume and PersistentVolumeClaim configurations
   - Mount the volume to `/data` in the Azurite container

## Cleanup

To remove all Kubernetes resources:

```bash
kubectl delete -f clickhouse-service.yaml
kubectl delete -f clickhouse-deployment.yaml
kubectl delete -f clickhouse-configmap.yaml
kubectl delete -f azurite-service.yaml
kubectl delete -f azurite-deployment.yaml
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

