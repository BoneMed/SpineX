# SpineX

SpineX C++/Qt application sources, Python components, server sources, and model weights.

## Usage and licensing / 使用与许可

Public visibility does not grant any additional right to use the project. See [LICENSE](LICENSE), [USAGE_POLICY.md](USAGE_POLICY.md), and [THIRD_PARTY_LICENSES.txt](THIRD_PARTY_LICENSES.txt). Existing permissions and third-party licenses remain in effect; this repository does not impose a blanket prohibition on third-party software. Gmsh is GPL-licensed and is linked by this project. Listing it as a dependency does not replace the obligations of its license.

公开展示不额外授予使用许可。原创内容、现有授权以及第三方组件的适用范围以各自许可证为准。Gmsh 的 GPL 权利和义务不因本仓库的声明而改变。

## Configuration

The uploaded copy reads sensitive settings from environment variables. `.env.example` lists their names; copy those settings into your process environment using your own credentials. No `.env` loader is added. The original local project has not been modified.

- Server: `DATABASE_URL`, `JWT_SECRET_KEY`, and `ADMIN_KEY`.
- Administrator creation: `SPINEX_ADMIN_EMAIL`, `SPINEX_ADMIN_PASSWORD`, optional `SPINEX_ADMIN_NAME`.
- Payments: `ALIPAY_PRIVATE_KEY` (literal `\n` escapes are converted to line breaks).
- Model encryption and application launch: `SPINEX_MODEL_KEY` (the Fernet key corresponding to the encrypted weights).
- Offline license generation: `SPINEX_LICENSE_SALT`.

Build output, executable binaries, embedded credentials, and generated source-code document exports are excluded. All files from `python/Model` are included, including encrypted weights and existing temporary checkpoint files. The Gmsh SDK runtime library is not bundled; obtain the required SDK under its applicable license from [Gmsh](https://gmsh.info/). Build configuration is in `CMakeLists.txt`; existing release notes are in `docs/RELEASE.md`.
