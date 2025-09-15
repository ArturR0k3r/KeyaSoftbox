# Contributing to KSB (Keya-Soft-Box)

We welcome contributions to the KSB project! This guide will help you get started.

## Development Setup

1. **Install Zephyr SDK**: Follow the [official Zephyr documentation](https://docs.zephyrproject.org/latest/develop/getting_started/index.html)

2. **Clone the repository**:
   ```bash
   git clone https://github.com/your-org/keya-soft-box.git
   cd keya-soft-box
   west update
   ```

3. **Set up development environment**:
   ```bash
   # Install dependencies
   pip3 install --user west pyelftools

   # Set environment variables
   export ZEPHYR_BASE=~/zephyrproject/zephyr
   export PATH="$HOME/.local/bin:$PATH"
   ```

## Code Style

- Follow the [Zephyr coding style](https://docs.zephyrproject.org/latest/contribute/guidelines.html#coding-style)
- Use descriptive variable and function names
- Add comments for complex algorithms
- Include documentation for public APIs
- Maximum line length: 100 characters

## Testing

Before submitting a pull request:

1. **Build for all supported boards**:
   ```bash
   west build -b esp32s3_devkitm
   west build -b esp32c3_devkitm  
   west build -b xiao_esp32c3
   ```

2. **Test hardware functionality**:
   - LED patterns work correctly
   - Mesh networking forms properly
   - Web configuration is accessible
   - Button input is responsive

3. **Check code quality**:
   ```bash
   # Static analysis (if available)
   cppcheck src/ include/

   # Memory usage analysis
   west build -t ram_report
   west build -t rom_report
   ```

## Submitting Changes

1. **Create a feature branch**:
   ```bash
   git checkout -b feature/your-feature-name
   ```

2. **Make your changes**:
   - Write clear, concise commit messages
   - Include tests for new functionality
   - Update documentation as needed

3. **Test thoroughly**:
   - Verify builds pass on all target boards
   - Test hardware functionality
   - Check for memory leaks or stability issues

4. **Submit a pull request**:
   - Provide a clear description of changes
   - Reference any related issues
   - Include screenshots/videos if applicable

## Reporting Issues

When reporting bugs or requesting features:

1. **Use the issue template**
2. **Include hardware information**:
   - Board type (ESP32-S3, ESP32-C3, XIAO)
   - Zephyr version
   - Build configuration

3. **Provide reproducible steps**
4. **Include logs and error messages**
5. **Add photos/videos if helpful**

## Code Review Process

1. All submissions require code review
2. Maintainers will provide feedback within 48 hours
3. Address review comments promptly
4. Once approved, changes will be merged

## Areas for Contribution

- **New LED patterns**: Add creative lighting effects
- **Mobile app**: Companion mobile application
- **Documentation**: Improve user guides and API docs
- **Hardware support**: Add support for new boards
- **Performance**: Optimize memory usage and speed
- **Testing**: Expand automated test coverage

## Questions?

- Open a GitHub issue for technical questions
- Email maintainers for project direction questions
- Join our community discussions

Thank you for contributing to KSB!