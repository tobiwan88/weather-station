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
    && rm -rf /var/lib/apt/lists/*

# Install Node.js LTS (required for Claude Code CLI)
RUN curl -fsSL https://deb.nodesource.com/setup_lts.x | bash - && \
    apt-get install -y --no-install-recommends nodejs && \
    rm -rf /var/lib/apt/lists/*


# Set zsh as default shell for the zephyr user
RUN chsh -s /usr/bin/zsh zephyr

# Install Claude Code CLI globally (npm package, not the curl installer)
RUN npm install -g @anthropic-ai/claude-code

# Switch back to zephyr user
USER zephyr
WORKDIR /home/zephyr/workspace/weather-station

# Optional: Set up oh-my-zsh for a better shell experience
RUN sh -c "$(curl -fsSL https://raw.githubusercontent.com/ohmyzsh/ohmyzsh/master/tools/install.sh)" || true

# End of Dockerfile
