#!/bin/bash
set -e

echo "Waiting for ClickHouse to init..."
until clickhouse-client --host "${CLICKHOUSE_HOST:-clickhouse}" --query "SELECT 1" > /dev/null 2>&1; do
  sleep 2
done
echo "ClickHouse ready."

echo "Tables in options_pricing:"
clickhouse-client --host "${CLICKHOUSE_HOST:-clickhouse}" \
  --query "SHOW TABLES FROM options_pricing" || true

cat <<'EOF'

------------------------------------------------------------------
 Dev environment ready.
   ClickHouse HTTP : http://clickhouse:8123   (host: localhost:8123)
   ClickHouse TCP  : clickhouse:9000          (host: localhost:9000)
   Grafana         : http://localhost:3000    (admin / admin)
   Raw data        : ./data/CME/...
------------------------------------------------------------------
EOF
