---
name: Bug report
about: Create a report to help us improve
title: ''
labels: bug
assignees: ''

---

**Describe the bug**

A clear and concise description of what the bug is.

**To Reproduce**

Steps to reproduce the behavior.

**Version**

What is the version of pgexporter ?

**PostgreSQL**

What is the version of PostgreSQL ?

**Event backend**

What is `ev_backend` in your configuration (if set), and on Linux was the binary built with liburing (io_uring) or epoll-only?

**OpenSSL**

What is the version of OpenSSL ?

**OS**

Which Operating System (OS) is used ?

**Configuration**

Can you provide the configuration pgexporter ?

* pgexporter.conf

**Prometheus**

Can you provide the output of the invalid Prometheus metrics ?

* Dump

**Debug logs**

Can you provide any debug logs (`log_level = debug5`) of the issue ?

**Tip**

Use \`\`\` before and after the text to keep the output as is.
