FROM zephyr-docker:arm

# Switch to root for installation steps
USER root

# Install zsh, curl, git, and other useful developer tools
RUN apt-get update && \
    apt-get install -y --no-install-recommends \
        zsh \
        git \
        curl \
        less \
        locales \
        sudo \
        vim \
        htop \
        tree \
        python3-pip \
        python3-venv \
        unzip \
        ca-certificates \
        screen \
    && rm -rf /var/lib/apt/lists/*

# Install TAP/bridge networking tools + general network utilities for native_sim
RUN apt-get update && \
    apt-get install -y --no-install-recommends \
        iproute2 \
        bridge-utils \
        iptables \
        iputils-ping \
        net-tools \
        dnsutils \
        chrony \
    && rm -rf /var/lib/apt/lists/*

# Install Node.js LTS (required for Claude Code CLI)
RUN curl -fsSL https://deb.nodesource.com/setup_lts.x | bash - && \
    apt-get install -y --no-install-recommends nodejs && \
    rm -rf /var/lib/apt/lists/*


# Set zsh as default shell for the zephyr user
RUN chsh -s /usr/bin/zsh zephyr

# Install Claude Code CLI globally (npm package, not the curl installer)
RUN npm install -g @anthropic-ai/claude-code

# Install pre-commit
RUN pip3 install --break-system-packages pre-commit

# Allow zephyr user to manage TAP interfaces without a password
RUN echo "zephyr ALL=(root) NOPASSWD: /usr/sbin/ip, /usr/sbin/sysctl, /sbin/iptables, /usr/sbin/iptables, /usr/sbin/chronyd" \
    > /etc/sudoers.d/zephyr-netadmin && \
    chmod 0440 /etc/sudoers.d/zephyr-netadmin

# Switch back to zephyr user
USER zephyr
WORKDIR /home/zephyr/workspace/weather-station

# Optional: Set up oh-my-zsh for a better shell experience
RUN sh -c "$(curl -fsSL https://raw.githubusercontent.com/ohmyzsh/ohmyzsh/master/tools/install.sh)" || true

# End of Dockerfile
