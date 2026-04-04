FROM ghcr.io/tobiwan88/zephyr_docker:arm

USER root

# 1. Combined Layer: System Tools + X11/Mesa + Cleanup
RUN apt-get update && apt-get install -y --no-install-recommends \
    zsh curl git sudo ca-certificates \
    less locales vim htop tree unzip pkg-config \
    python3-pip python3-venv \
    libsdl2-dev libx11-6 libxext6 x11-apps xauth \
    libgl1-mesa-dri libglx-mesa0 mesa-utils imagemagick \
		xvfb x11vnc clangd clang clang-format clang-tidy \
    && apt-get clean && rm -rf /var/lib/apt/lists/*
# 3. User Setup & Zsh Permissions
RUN chsh -s /usr/bin/zsh zephyr && \
    echo "zephyr ALL=(ALL) NOPASSWD:ALL" >> /etc/sudoers

USER zephyr
WORKDIR /home/zephyr/workspace/weather-station

# 4. Zsh Setup (Unattended to prevent build hangs) and claude.ai CLI
RUN sh -c "$(curl -fsSL https://raw.githubusercontent.com/ohmyzsh/ohmyzsh/master/tools/install.sh)" "" --unattended && \
		curl -fsSL https://claude.ai/install.sh | bash  && \
		echo 'export PATH="$HOME/.local/bin:$PATH"' >> ~/.zshrc

# 5. Environment Variables
ENV DISPLAY=:1 \
    LIBGL_ALWAYS_SOFTWARE=1 \
    SHELL=/usr/bin/zsh \
    TERM=xterm-256color

# Start in Zsh
CMD ["/usr/bin/zsh"]
