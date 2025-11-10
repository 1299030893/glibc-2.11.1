# cat_name 在 __open_catalog 中的使用路径分析

## 函数签名

```c
int __open_catalog (const char *cat_name, const char *nlspath, 
                    const char *env_var, __nl_catd catalog)
```

## cat_name 的使用位置

### 位置 1: 直接路径检查 【open_catalog.c:52-53】

```c
if (strchr (cat_name, '/') != NULL || nlspath == NULL)
  fd = open_not_cancel_2 (cat_name, O_RDONLY);
```

**说明**:
- 如果 `cat_name` 包含 `/`，说明是绝对路径或相对路径
- 直接使用 `cat_name` 打开文件
- ✅ **不涉及栈分配**，不会导致栈溢出

---

### 位置 2: 相邻冒号处理 【open_catalog.c:83-90】

```c
if (*run_nlspath == ':')
  {
    /* Leading colon or adjacent colons - treat same as %N.  */
    len = strlen (cat_name);        // ← 计算 cat_name 长度
    ENOUGH (len);                   // ← 触发栈分配
    memcpy (&buf[bufact], cat_name, len);  // ← 将 cat_name 复制到 buf
    bufact += len;
  }
```

**说明**:
- 当 NLSPATH 中有相邻的冒号（如 `::` 或开头是 `:`）
- 将 `cat_name` 直接复制到栈缓冲区 `buf`
- ⚠️ **涉及栈分配**，如果 `cat_name` 很长会导致栈溢出

**示例**:
```bash
export NLSPATH=":%N:%N:%N"  # 相邻的冒号
catopen("A" * 50000, 0)  # 50000字节的 cat_name
# → 每次遇到 ':'，都会将完整的 cat_name 复制到 buf
# → buf 大小 = 50000 * N（N 个相邻冒号）
```

---

### 位置 3: %N 占位符替换 【open_catalog.c:100-106】

```c
case 'N':
  /* Use the catalog name.  */
  len = strlen (cat_name);        // ← 计算 cat_name 长度
  ENOUGH (len);                   // ← 触发栈分配
  memcpy (&buf[bufact], cat_name, len);  // ← 将 cat_name 复制到 buf
  bufact += len;
  break;
```

**说明**:
- 当 NLSPATH 中包含 `%N` 占位符时
- 将 `cat_name` 替换到 `%N` 的位置
- ⚠️ **涉及栈分配**，如果 `cat_name` 很长或 NLSPATH 中有多个 `%N` 会导致栈溢出

**示例**:
```bash
export NLSPATH="/path/%N/%N/%N/%N/%N"  # 5个 %N
catopen("A" * 10000, 0)  # 10000字节的 cat_name
# → 每个 %N 都会被替换为完整的 cat_name
# → buf 大小 ≈ 5 * 10000 = 50KB
```

---

## 完整的 cat_name 使用流程

```
__open_catalog(cat_name, nlspath, env_var, result)
│
├─ 检查: cat_name 是否包含 '/'? 【open_catalog.c:52】
│  │
│  ├─ [是] → 直接使用 cat_name 打开文件 【open_catalog.c:53】
│  │  └─ open_not_cancel_2(cat_name, O_RDONLY)
│  │     └─ ✅ 不涉及栈分配
│  │
│  └─ [否] → 进入 NLSPATH 解析流程
│     │
│     └─ 初始化栈缓冲区
│        char *buf = NULL;
│        size_t bufmax = 0;
│        │
│        └─ 循环处理 NLSPATH 的每个路径元素 【open_catalog.c:79-187】
│           │
│           ├─ 情况 A: 遇到相邻冒号 ':' 【open_catalog.c:83-90】
│           │  └─ len = strlen(cat_name);
│           │     ENOUGH(len);  ← 栈分配
│           │     memcpy(&buf[bufact], cat_name, len);  ← cat_name → buf
│           │
│           ├─ 情况 B: 遇到 '%N' 占位符 【open_catalog.c:100-106】
│           │  └─ len = strlen(cat_name);
│           │     ENOUGH(len);  ← 栈分配
│           │     memcpy(&buf[bufact], cat_name, len);  ← cat_name → buf
│           │
│           └─ 情况 C: 其他字符或占位符
│              └─ 不涉及 cat_name
│
│           └─ 尝试打开文件: open_not_cancel_2(buf, O_RDONLY) 【open_catalog.c:181】
│              └─ 如果成功，退出循环
```

## 栈溢出触发条件

### 条件 1: cat_name 很长 + 相邻冒号

```c
// NLSPATH = ":%N:%N:%N"  (3个相邻冒号)
// cat_name = "A" * 50000  (50000字节)

// 第1次遇到 ':' → buf 增长 50000 字节
// 第2次遇到 ':' → buf 增长 50000 字节  
// 第3次遇到 ':' → buf 增长 50000 字节
// 最终 buf 大小 ≈ 150KB → 栈溢出
```

### 条件 2: cat_name 很长 + 多个 %N

```c
// NLSPATH = "/path/%N/%N/%N/%N/%N"  (5个 %N)
// cat_name = "A" * 10000  (10000字节)

// 第1个 %N → buf 增长 10000 字节
// 第2个 %N → buf 增长 10000 字节
// 第3个 %N → buf 增长 10000 字节
// 第4个 %N → buf 增长 10000 字节
// 第5个 %N → buf 增长 10000 字节
// 最终 buf 大小 ≈ 50KB → 栈溢出
```

### 条件 3: cat_name 中等长度 + 很多 %N

```c
// NLSPATH = "/%N/%N/%N/...%N"  (100个 %N)
// cat_name = "A" * 1000  (1000字节)

// 每个 %N → buf 增长 1000 字节
// 最终 buf 大小 ≈ 100KB → 栈溢出
```

## 代码追踪

### 关键代码片段 1: 相邻冒号处理

```c
// open_catalog.c:83-90
if (*run_nlspath == ':')
  {
    /* Leading colon or adjacent colons - treat same as %N.  */
    len = strlen (cat_name);        // ← cat_name 长度
    ENOUGH (len);                   // ← 触发 alloca(bufmax)
    memcpy (&buf[bufact], cat_name, len);  // ← 复制 cat_name
    bufact += len;
  }
```

### 关键代码片段 2: %N 占位符处理

```c
// open_catalog.c:100-106
case 'N':
  /* Use the catalog name.  */
  len = strlen (cat_name);        // ← cat_name 长度
  ENOUGH (len);                   // ← 触发 alloca(bufmax)
  memcpy (&buf[bufact], cat_name, len);  // ← 复制 cat_name
  bufact += len;
  break;
```

### 关键代码片段 3: ENOUGH 宏（栈分配）

```c
// open_catalog.c:57-64
#define ENOUGH(n)							      \
  if (__builtin_expect (bufact + (n) >= bufmax, 0))			      \
    {									      \
      char *old_buf = buf;						      \
      bufmax += 256 + (n);						      \
      buf = (char *) alloca (bufmax);  // ← 栈分配，动态增长
      memcpy (buf, old_buf, bufact);					      \
    }
```

## 总结

**cat_name 在 __open_catalog 中的使用**:

1. **直接使用** (open_catalog.c:53): 
   - 如果 `cat_name` 包含 `/`，直接打开文件
   - ✅ 不涉及栈分配

2. **复制到栈缓冲区** (open_catalog.c:86-89, 102-105):
   - 遇到相邻冒号 `:` 时
   - 遇到 `%N` 占位符时
   - ⚠️ 涉及栈分配，可能导致栈溢出

**栈溢出风险**:
- `cat_name` 长度 × NLSPATH 中 `%N` 或相邻冒号的数量
- 如果 `cat_name` 很长，或 NLSPATH 中有很多 `%N`，`buf` 会快速增长
- `buf` 使用 `alloca()` 在栈上分配，没有上限检查
- 最终可能导致栈溢出
