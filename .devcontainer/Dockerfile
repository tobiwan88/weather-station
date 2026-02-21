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

# Set zsh as default shell for the zephyr user
RUN chsh -s /usr/bin/zsh zephyr

# Switch back to zephyr user
USER zephyr
WORKDIR /home/zephyr/workspace/weather-station

# Optional: Set up oh-my-zsh for a better shell experience
RUN sh -c "$(curl -fsSL https://raw.githubusercontent.com/ohmyzsh/ohmyzsh/master/tools/install.sh)" || true

# Optional: Install Claude CLI (if available as a script or pip package)
# Example for a bash installer (replace with actual install if different)
RUN curl -fsSL https://claude.ai/install.sh | bash || true


# Ensure venv is created and requirements installed on container creation
# (devcontainer.json will still handle this via onCreateCommand)

# Additional developer tools can be added here as needed
# e.g., RUN pip install ipython black flake8

# End of Dockerfile
