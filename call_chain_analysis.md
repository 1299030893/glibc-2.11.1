# CVE-2015-8779 调用链分析 - name 大小展示

## 调用链结构

```
isc_msgcat_open(name,...)【msgcat.c】
├─REQUIRE(name != NULL);
└─msgcat->catalog = catopen(name, 0);
  └─catopen(cat_name, flag)【catgets.c】
    ├─/* 展示 name 的大小 */
    │  {
    │    size_t name_size = cat_name ? strlen (cat_name) : 0;
    │    dprintf (2, "[catopen] cat_name size: %zu, flag: %d\n", name_size, flag);
    │  }
    ├─if (strchr (cat_name, '/') == NULL)
    │  └─处理 NLSPATH 逻辑
    └─__open_catalog(cat_name, nlspath, env_var, result)
      └─__open_catalog(cat_name, nlspath, env_var, catalog)【open_catalog.c】
        ├─/* 展示 name 的大小 */
        │  {
        │    size_t name_size = cat_name ? strlen (cat_name) : 0;
        │    dprintf (2, "[__open_catalog] cat_name size: %zu, nlspath: %s\n", name_size, nlspath ? "present" : "NULL");
        │  }
        ├─if (strchr (cat_name, '/') != NULL || nlspath == NULL)
        │  └─fd = open_not_cancel_2 (cat_name, O_RDONLY);
        └─else
          └─处理 NLSPATH 循环
            ├─if (*run_nlspath == ':')
            │  ├─len = strlen (cat_name);
            │  ├─/* 展示 name 的大小（在 alloca 调用前） */
            │  │  dprintf (2, "[__open_catalog] : case: cat_name size: %zu, will allocate bufmax: %zu\n", len, bufmax);
            │  └─ENOUGH (len);
            │     └─#define ENOUGH(n)
            │        ├─if (bufact + (n) >= bufmax)
            │        │  ├─bufmax += 256 + (n);
            │        │  ├─/* 展示 alloca 分配的大小（CVE-2015-8779 关键点） */
            │        │  │  dprintf (2, "[__open_catalog] ENOUGH: allocating %zu bytes via alloca (old: %zu, need: %zu)\n", bufmax, old_bufmax, (n));
            │        │  └─buf = (char *) alloca (bufmax);  // ⚠️ CVE-2015-8779 风险点
            │        └─memcpy (buf, old_buf, bufact);
            └─while (*run_nlspath != ':' && *run_nlspath != '\0')
              └─if (*run_nlspath == '%')
                └─switch (*run_nlspath++)
                  └─case 'N':
                    ├─len = strlen (cat_name);
                    ├─/* 展示 name 的大小（在 alloca 调用前） */
                    │  dprintf (2, "[__open_catalog] %%N case: cat_name size: %zu, current bufmax: %zu\n", len, bufmax);
                    └─ENOUGH (len);
                       └─同上 ENOUGH 宏展开
```

## 关键代码位置

### 1. catopen() 函数入口【catgets.c:35-41】

```c
nl_catd
catopen (const char *cat_name, int flag)
{
  /* 展示 name 的大小（CVE-2015-8779 调查：分析可能的最大值） */
  {
    size_t name_size = cat_name ? strlen (cat_name) : 0;
    dprintf (2, "[catopen] cat_name size: %zu, flag: %d\n", name_size, flag);
  }
  ...
}
```

### 2. __open_catalog() 函数入口【open_catalog.c:40-48】

```c
int
__open_catalog (const char *cat_name, const char *nlspath, const char *env_var,
		__nl_catd catalog)
{
  /* 展示 name 的大小（CVE-2015-8779 调查：分析可能的最大值） */
  {
    size_t name_size = cat_name ? strlen (cat_name) : 0;
    dprintf (2, "[__open_catalog] cat_name size: %zu, nlspath: %s\n", name_size, nlspath ? "present" : "NULL");
  }
  ...
}
```

### 3. ENOUGH 宏定义【open_catalog.c:64-74】

```c
#define ENOUGH(n)							      \
  if (__builtin_expect (bufact + (n) >= bufmax, 0))			      \
    {									      \
      char *old_buf = buf;						      \
      size_t old_bufmax = bufmax;					      \
      bufmax += 256 + (n);						      \
      /* 展示 alloca 分配的大小（CVE-2015-8779 关键点） */		      \
      dprintf (2, "[__open_catalog] ENOUGH: allocating %zu bytes via alloca (old: %zu, need: %zu)\n", bufmax, old_bufmax, (n)); \
      buf = (char *) alloca (bufmax);					      \
      memcpy (buf, old_buf, bufact);					      \
    }
```

### 4. 处理 ':' 的情况【open_catalog.c:93-99】

```c
if (*run_nlspath == ':')
  {
    /* Leading colon or adjacent colons - treat same as %N.  */
    len = strlen (cat_name);
    /* 展示 name 的大小（在 alloca 调用前） */
    dprintf (2, "[__open_catalog] : case: cat_name size: %zu, will allocate bufmax: %zu\n", len, bufmax);
    ENOUGH (len);
    memcpy (&buf[bufact], cat_name, len);
    bufact += len;
  }
```

### 5. 处理 '%N' 的情况【open_catalog.c:112-117】

```c
case 'N':
  /* Use the catalog name.  */
  len = strlen (cat_name);
  /* 展示 name 的大小（在 alloca 调用前） */
  dprintf (2, "[__open_catalog] %%N case: cat_name size: %zu, current bufmax: %zu\n", len, bufmax);
  ENOUGH (len);
  memcpy (&buf[bufact], cat_name, len);
  bufact += len;
  break;
```

## CVE-2015-8779 风险分析

**漏洞位置**: `__open_catalog()` 函数中的 `ENOUGH` 宏使用 `alloca()` 分配栈内存

**风险点**:
- 当 `cat_name` 长度很大时，`ENOUGH(len)` 会通过 `alloca()` 分配 `bufmax = 256 + len` 字节的栈空间
- 如果 `cat_name` 长度超过栈限制，会导致栈溢出（Stack Overflow）
- 攻击者可以通过传递超长的 `cat_name` 参数触发 DoS

**调试输出位置**:
1. `catopen()` 入口：显示传入的 `cat_name` 大小
2. `__open_catalog()` 入口：显示 `cat_name` 大小和 `nlspath` 状态
3. `ENOUGH` 宏内部：显示每次 `alloca` 分配的实际字节数（关键）
4. 使用 `cat_name` 的地方（`:` 和 `%N` 情况）：显示在分配前的 `cat_name` 大小

这些调试输出可以帮助分析实际使用中 `name` 参数的最大可能大小，从而评估 CVE-2015-8779 的影响范围。
