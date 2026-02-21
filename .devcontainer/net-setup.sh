#!/usr/bin/env bash
# net-setup.sh — Create zeth0 TAP interface + NAT for native_sim internet access
# Usage: .devcontainer/net-setup.sh [TAP_IFACE]   (default: zeth0)
set -euo pipefail

TAP_IF="${1:-zeth0}"
HOST_ADDR="192.0.2.1"
SIM_SUBNET="192.0.2.0/24"

# Tear down stale interface if present
if ip link show "$TAP_IF" &>/dev/null; then
    sudo ip link set "$TAP_IF" down
    sudo ip tuntap del dev "$TAP_IF" mode tap
fi

# Create TAP owned by the current user so native_sim can open it without root
sudo ip tuntap add dev "$TAP_IF" mode tap user "$(id -un)"
sudo ip link set "$TAP_IF" up
sudo ip addr add "$HOST_ADDR/24" dev "$TAP_IF"

# Enable IPv4 forwarding + masquerade so native_sim reaches the internet
sudo sysctl -qw net.ipv4.ip_forward=1
# Remove duplicate rule if re-run, then re-add
sudo iptables -t nat -D POSTROUTING -s "$SIM_SUBNET" ! -o "$TAP_IF" -j MASQUERADE 2>/dev/null || true
sudo iptables -t nat -A POSTROUTING -s "$SIM_SUBNET" ! -o "$TAP_IF" -j MASQUERADE

echo "TAP interface '$TAP_IF' ready."
echo "  Host side : $HOST_ADDR"
echo "  native_sim: 192.0.2.2  (set CONFIG_NET_CONFIG_MY_IPV4_ADDR)"
echo "  Gateway   : $HOST_ADDR  (set CONFIG_NET_CONFIG_MY_IPV4_GW)"
