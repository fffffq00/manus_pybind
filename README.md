# Manus SDK Python 绑定包编译项目 (`manus_pybind`)

本仓库提供了针对 **Manus Glove Core SDK** C++ 客户端的 Python 绑定包 (`manus_pybind`) 编译环境。项目使用 `pybind11` 实现 C++ 到 Python 的接口映射，并通过 Docker 和 `auditwheel` 打包出一个**完全自包含（Self-contained）**的 Python Wheel (`.whl`) 安装包。

编译出的 Wheel 包已自动打包了所有的系统底层依赖项（如 `libprotobuf`、`libgrpc`、`libzmq`、`libusb` 等），只需在目标机器上执行 `pip install` 即可在任意 Python 环境中直接导入并使用。

---

## 1. 编译前准备

### 1.1 依赖工具
* **宿主机环境**：需要安装并配置好 **Docker**。
* 如果您在宿主机上使用了本地 network 代理，编译脚本会自动识别并将其传递给 Docker 容器，确保拉取依赖和 pyenv 安装 Python 时能够顺利访问网络。

### 1.2 手动下载官方 Manus SDK
本仓库不包含商业闭源的 Manus SDK 动态库和头文件。您需要从 Manus 开发者官网下载 **Manus SDK Client for Linux**（例如 `ManusSDK_v3.1.1`）。

解压后，请**直接将 `ManusSDK` 目录完整拷贝到本仓库根目录下**。编译脚本和 Python 安装程序会自动识别并加载其中的头文件和库文件，无需手动拆分。

---

## 2. 目录放置与结构规划

将下载好的 `ManusSDK` 文件夹直接放入本仓库根目录后，整体目录结构如下所示：

```text
manus_pybind_github/
├── .gitignore
├── Dockerfile.build          # Docker 基础编译镜像定义
├── MANIFEST.in               # Wheel 打包资源清单
├── README.md                 # 说明文档
├── build_in_docker.sh        # [宿主机执行] 一键编译脚本
├── build_wheel.sh            # [容器内执行] 编译核心脚本
├── pyproject.toml            # Python 编译依赖声明
├── setup.py                  # Python setuptools 编译配置
│
├── ManusSDK/                 # 👈 ⚠️ 直接将官方 SDK 文件夹整体拷贝到这里！
│   ├── include/              # 包含 SDK.h, SDKTypes.h 等头文件
│   └── lib/                  # 包含 libManusSDK.so, libManusSDK_Integrated.so 动态链接库
│
├── example/                  # 示例代码
│   └── visualize.py          # 3D 骨骼与关节实时数据可视化测试脚本
│
├── manus_pybind/             # Python 包结构
│   ├── __init__.py           # 包入口
│   └── manus_pybind.pyi      # Python 类型存根 (Type Stubs，用于 IDE 代码补全)
│
└── src/
    └── ManusClientPybind.cpp # C++ 绑定核心源码
```

---

## 3. 编译流程

放置好 `ManusSDK` 目录后，按照以下步骤进行编译：

### 3.1 一键编译
在宿主机仓库根目录下运行编译脚本，可以指定您所需要的 Python 版本（例如 `3.13` 或 `3.14`）：

```bash
# 赋予脚本执行权限（如果还没有的话）
chmod +x build_in_docker.sh build_wheel.sh

# 编译针对 Python 3.13 的 Wheel
./build_in_docker.sh 3.13

# 编译针对 Python 3.14 的 Wheel
./build_in_docker.sh 3.14
```

* **首次运行提示**：首次编译时，Docker 会下载并构建编译基础镜像（在容器内源码编译安装 gRPC 和 Protobuf 约需 5~10 分钟）；同时会通过 `pyenv` 编译指定版本的 Python 源码，这也需要一些时间。
* **快速二次编译**：在您首次成功编译某一个 Python 版本后，该版本对应的 Python 环境会永久缓存到本地宿主机的 `.pyenv_versions/` 目录中。下次再运行相同 Python 版本的编译时，将直接跳过 Python 源码编译阶段，在数秒内即可完成 Wheel 的重新打包。

---

## 4. 编译输出与安装

### 4.1 输出文件
编译完成后，修复后符合 `manylinux` 标准的自包含 Wheel 包将输出到宿主机的 `wheelhouse/` 目录下：

```text
wheelhouse/
└── manus_pybind-0.1.0-cp314-cp314-manylinux_2_31_x86_64.whl
```

### 4.2 安装 Wheel 包
在宿主机（或任何需要使用的 Linux 目标机器）上，直接安装生成的包：

```bash
# 安装生成的 Wheel 包
pip install --force-reinstall wheelhouse/manus_pybind-*.whl
```

---

## 5. 示例运行测试

项目在 `example/` 目录中提供了一个实时的交互式 3D 手套关节骨骼数据可视化脚本，它使用了您编译出的 `manus_pybind` 包。

### 5.1 安装可视化依赖
```bash
pip install matplotlib
```

### 5.2 运行可视化
确保您的 Manus Core 服务已在局域网内或本机启动，然后运行：

```bash
# 默认 3D 骨骼可视化 (自动扫描并连接网络中的 Manus Core 客户端)
python example/visualize.py

# 2D 条形图显示手指各关节 stretch / spread 弯曲度数据
python example/visualize.py --ergo

# 指定连接的 Manus Core 服务 IP
python example/visualize.py --host 192.168.1.100
```
