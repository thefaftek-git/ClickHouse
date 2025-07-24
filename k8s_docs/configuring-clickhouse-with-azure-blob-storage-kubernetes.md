








# Configuring ClickHouse with Azure Blob Storage in Kubernetes (Using Azurite)

This guide provides step-by-step instructions for configuring ClickHouse to use Azure Blob Storage as a backend in a Kubernetes environment, using Azurite (Azure Storage emulator) instead of actual cloud-hosted Azure Blob Storage.

## Prerequisites

1. **Kubernetes Cluster**: A running Kubernetes cluster (we use KIND - Kubernetes IN Docker)
2. **kubectl**: Kubernetes command-line tool configured to connect to your cluster
3. **Azurite**: Azure Blob Storage emulator deployed in Kubernetes
4. **ClickHouse**: ClickHouse server deployed in Kubernetes

## 1. Deploy Azurite (Azure Blob Storage Emulator)

### Create Azurite Deployment Manifest

```yaml
# azurite-deployment.yaml
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
        env:
        - name: AZURE_STORAGE_ACCOUNTS
          value: "azurite:key=Eby8vdM02xNOcqFlqUwJPLlmEtlCDXJ1OUzFT50uSRZ6IFsuFq2UVErCz4I6tq/K1SZFPTOtr/KBHBeksoGMGw==:connectionString=DefaultEndpointsProtocol=http;AccountName=azurite;AccountKey=Eby8vdM02xNOcqFlqUwJPLlmEtlCDXJ1OUzFT50uSRZ6IFsuFq2UVErCz4I6tq/K1SZFPTOtr/KBHBeksoGMGw==;BlobEndpoint=http://azurite:10000/devstoreaccount1;"
```

### Create Azurite Service Manifest

```yaml
# azurite-service.yaml
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
    targetPort: 10000
```

### Deploy Azurite

```bash
kubectl apply -f azurite-deployment.yaml
kubectl apply -f azurite-service.yaml
```

## 2. Configure ClickHouse for Azure Blob Storage

### Create ClickHouse Configuration Files

#### Main Configuration (`config.xml`)

```xml
<yandex>
    <!-- Other configurations -->

    <remote_servers>
        <azurite>
            <endpoint>
                <host>azurite</host>
                <port>10000</port>
            </endpoint>
            <user>azurite</user>
            <password>Eby8vdM02xNOcqFlqUwJPLlmEtlCDXJ1OUzFT50uSRZ6IFsuFq2UVErCz4I6tq/K1SZFPTOtr/KBHBeksoGMGw==</password>
            <default_profile>s3</default_profile>
        </azurite>
    </remote_servers>

    <!-- Other configurations -->
</yandex>
```

#### Users Configuration (`users.xml`)

```xml
<yandex>
    <users>
        <default>
            <profile>default</profile>
            <quota>default</quota>
            <storage_configuration>s3</storage_configuration>
        </default>
    </users>

    <profiles>
        <default>
            <storage_configuration>s3</storage_configuration>
        </default>
    </profiles>

    <quotas>
        <default>
            <tables_max_number>1000000</tables_max_number>
        </default>
    </quotas>

    <storage_configurations>
        <s3>
            <type>s3</type>
            <endpoint>http://azurite:10000/devstoreaccount1</endpoint>
            <access_key_id>azurite</access_key_id>
            <secret_access_key>Eby8vdM02xNOcqFlqUwJPLlmEtlCDXJ1OUzFT50uSRZ6IFsuFq2UVErCz4I6tq/K1SZFPTOtr/KBHBeksoGMGw==</secret_access_key>
            <region>us-east-1</region>
        </s3>
    </storage_configurations>
</yandex>
```

#### Metadata Configuration (`metadata_type_local.xml`)

```xml
<yandex>
    <!-- Other configurations -->

    <remote_servers>
        <azurite>
            <endpoint>
                <host>azurite</host>
                <port>10000</port>
            </endpoint>
            <user>azurite</user>
            <password>Eby8vdM02xNOcqFlqUwJPLlmEtlCDXJ1OUzFT50uSRZ6IFsuFq2UVErCz4I6tq/K1SZFPTOtr/KBHBeksoGMGw==</password>
            <default_profile>s3</default_profile>
        </azurite>
    </remote_servers>

    <!-- Other configurations -->
</yandex>
```

### Create ConfigMap for ClickHouse Configuration

```yaml
# clickhouse-configmap.yaml
apiVersion: v1
kind: ConfigMap
metadata:
  name: clickhouse-config
data:
  config.xml: |
    <yandex>
        <!-- Main ClickHouse configuration -->
        <listen_host>0.0.0.0</listen_host>

        <remote_servers>
            <azurite>
                <endpoint>
                    <host>azurite</host>
                    <port>10000</port>
                </endpoint>
                <user>azurite</user>
                <password>Eby8vdM02xNOcqFlqUwJPLlmEtlCDXJ1OUzFT50uSRZ6IFsuFq2UVErCz4I6tq/K1SZFPTOtr/KBHBeksoGMGw==</password>
                <default_profile>s3</default_profile>
            </azurite>
        </remote_servers>

        <!-- Other configurations -->
    </yandex>

  users.xml: |
    <yandex>
        <users>
            <default>
                <profile>default</profile>
                <quota>default</quota>
                <storage_configuration>s3</storage_configuration>
            </default>
        </users>

        <profiles>
            <default>
                <storage_configuration>s3</storage_configuration>
            </default>
        </profiles>

        <quotas>
            <default>
                <tables_max_number>1000000</tables_max_number>
            </default>
        </quotas>

        <storage_configurations>
            <s3>
                <type>s3</type>
                <endpoint>http://azurite:10000/devstoreaccount1</endpoint>
                <access_key_id>azurite</access_key_id>
                <secret_access_key>Eby8vdM02xNOcqFlqUwJPLlmEtlCDXJ1OUzFT50uSRZ6IFsuFq2UVErCz4I6tq/K1SZFPTOtr/KBHBeksoGMGw==</secret_access_key>
                <region>us-east-1</region>
            </s3>
        </storage_configurations>
    </yandex>

  metadata_type_local.xml: |
    <yandex>
        <!-- Metadata configuration -->
        <remote_servers>
            <azurite>
                <endpoint>
                    <host>azurite</host>
                    <port>10000</port>
                </endpoint>
                <user>azurite</user>
                <password>Eby8vdM02xNOcqFlqUwJPLlmEtlCDXJ1OUzFT50uSRZ6IFsuFq2UVErCz4I6tq/K1SZFPTOtr/KBHBeksoGMGw==</password>
                <default_profile>s3</default_profile>
            </azurite>
        </remote_servers>

        <!-- Other configurations -->
    </yandex>
```

## 3. Deploy ClickHouse with Azure Blob Storage Configuration

### Create ClickHouse Deployment Manifest

```yaml
# clickhouse-deployment.yaml
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
        env:
        - name: CLICKHOUSE_USER
          value: "default"
        - name: CLICKHOUSE_PASSWORD
          value: ""
        - name: CLICKHOUSE_DEFAULT_ACCESS_MANAGEMENT
          value: "1"
        - name: CLICKHOUSE_LISTEN_HOST
          value: "0.0.0.0"
        volumeMounts:
        - name: clickhouse-config
          mountPath: /etc/clickhouse-server/
      volumes:
      - name: clickhouse-config
        configMap:
          name: clickhouse-config
```

### Create ClickHouse Service Manifest

```yaml
# clickhouse-service.yaml
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
    targetPort: 8123
    name: http-port
  - protocol: TCP
    port: 9000
    targetPort: 9000
    name: native-port
```

### Deploy ClickHouse

```bash
kubectl apply -f clickhouse-configmap.yaml
kubectl apply -f clickhouse-deployment.yaml
kubectl apply -f clickhouse-service.yaml
```

## 4. Testing the Configuration

### Create a Test Pod for Connectivity Testing

```yaml
# test-pod.yaml
apiVersion: v1
kind: Pod
metadata:
  name: test-pod
spec:
  containers:
  - name: test-container
    image: busybox
    command: ["sleep", "3600"]
  restartPolicy: Never
```

```bash
kubectl apply -f test-pod.yaml
```

### Test Connectivity to ClickHouse

```bash
kubectl exec test-pod -- wget http://clickhouse.default.svc.cluster.local:8123
```

### Test Connectivity to Azurite

```bash
kubectl exec test-pod -- wget http://azurite.default.svc.cluster.local:10000
```

## 5. Create Tables Using Azure Blob Storage Backend

Once ClickHouse is running with the proper configuration, you can create tables that use Azure Blob Storage as their backend:

```sql
CREATE TABLE my_table (
    id UInt32,
    name String,
    value Float32
) ENGINE = MergeTree()
ORDER BY id;

-- Create a table using S3 (Azure Blob Storage via Azurite)
CREATE TABLE my_s3_table (
    id UInt32,
    name String,
    value Float32
) ENGINE = MergeTree()
ORDER BY id
SETTINGS storage_policy = 's3';
```

## Troubleshooting

### Common Issues and Solutions

1. **ClickHouse Network Access Disabled**
   - Error: `neither CLICKHOUSE_USER nor CLICKHOUSE_PASSWORD is set, disabling network access for user 'default'`
   - Solution: Set environment variables `CLICKHOUSE_USER` and `CLICKHOUSE_PASSWORD`

2. **ClickHouse Not Listening on All Interfaces**
   - Error: Connection refused when connecting to service IP
   - Solution: Set environment variable `CLICKHOUSE_LISTEN_HOST=0.0.0.0`

3. **DNS Resolution Issues in Kubernetes**
   - Error: Service name resolution not working (`clickhouse.default.svc.cluster.local` fails)
   - Workaround: Use direct pod IPs or service ClusterIPs

4. **Azurite Connection Problems**
   - Ensure Azurite is running and accessible on port 10000
   - Verify connection string and credentials in ClickHouse configuration

5. **ClickHouse Configuration Errors**
   - Check ClickHouse logs for configuration parsing errors
   - Validate XML configuration files using online validators or tools

## Conclusion

This guide provides a complete setup for configuring ClickHouse with Azure Blob Storage (using Azurite emulator) in a Kubernetes environment. The key components are:

1. **Azurite**: Deployed as a Kubernetes service providing Azure Blob Storage functionality
2. **ClickHouse**: Configured to use Azurite as its storage backend via S3-compatible configuration
3. **Kubernetes Manifests**: All configurations packaged as ConfigMaps and deployments for easy management

By following these steps, you can successfully integrate ClickHouse with Azure Blob Storage in a Kubernetes environment using Azurite for local testing.





