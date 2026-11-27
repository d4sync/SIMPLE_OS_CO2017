#include "loader.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t avail_pid = 1;
#define OPT_CALC	"calc"
#define OPT_ALLOC	"alloc"
#define OPT_FREE	"free"
#define OPT_READ	"read"
#define OPT_WRITE	"write"
#define OPT_SYSCALL	"syscall"
#define OPT_KMALLOC	            "kmalloc"
#define OPT_DMA_MEMCPY	        "dma_memcpy"
#define OPT_COPY_FROM_USER	    "copy_from_user"
#define OPT_COPY_TO_USER	    "copy_to_user"

/* get_opcode — Đổi chuỗi opcode trong file mô tả process sang enum.
 * Tham số: opt: tên instruction.
 * Trả về: Giá trị enum opcode; thoát chương trình nếu opcode không hợp lệ.
 */
static enum ins_opcode_t get_opcode(char * opt)
{
    if (!strcmp(opt, OPT_CALC)) {
        return CALC;
    } else if (!strcmp(opt, OPT_ALLOC)) {
        return ALLOC;
    } else if (!strcmp(opt, OPT_FREE)) {
        return FREE;
    } else if (!strcmp(opt, OPT_READ)) {
        return READ;
    } else if (!strcmp(opt, OPT_WRITE)) {
        return WRITE;
    } else if (!strcmp(opt, OPT_KMALLOC)) {
        return KMALLOC;
    } else if (!strcmp(opt, OPT_COPY_FROM_USER)) {
        return COPY_FROM_USER;
    } else if (!strcmp(opt, OPT_COPY_TO_USER)) {
        return COPY_TO_USER;
    } else if (!strcmp(opt, OPT_DMA_MEMCPY)) {
        return DMA_MEMCPY;
    } else if (!strcmp(opt, OPT_SYSCALL)) {
        return SYSCALL;
    } else {
        printf("get_opcode return Opcode: %s\n", opt);
        exit(1);
    }
}

/* load — Đọc file process, tạo PCB/code segment và kiểm tra số đối số từng instruction.
 * Tham số: path: đường dẫn file process.
 * Trả về: Con trỏ PCB mới; thoát chương trình nếu file lỗi.
 */
struct pcb_t * load(const char * path)
{
    /* Create new PCB for the new process */
    struct pcb_t * proc = (struct pcb_t *)calloc(1, sizeof(struct pcb_t));
    if (!proc) {
        perror("PCB calloc");
        exit(1);
    }
    proc->pid = avail_pid;
    avail_pid++;
#ifndef MM_PAGING
    proc->page_table = calloc(1, sizeof(struct page_table_t));
#endif
    proc->bp = PAGE_SIZE;
    proc->pc = 0;
    /* Read process code from file */
    FILE * file;
    if ((file = fopen(path, "r")) == NULL) {
        printf("Cannot find process description at '%s'\n", path);
        exit(1);
    }
    snprintf(proc->path, sizeof(proc->path), "%s", path);
    char line[256], opcode[32], extra;
    proc->code = calloc(1, sizeof(*proc->code));
    if (!proc->code || !fgets(line, sizeof(line), file) || sscanf(line, "%u %u %c", &proc->priority, &proc->code->size, &extra) != 2) {
        goto invalid;
    }
    proc->code->text = calloc(proc->code->size, sizeof(struct inst_t));
    if (proc->code->size && !proc->code->text) {
        goto invalid;
    }
    for (uint32_t i = 0; i < proc->code->size; ++i) {
        struct inst_t *instruction = &proc->code->text[i];
        do {
            if (!fgets(line, sizeof(line), file)) {
                goto invalid;
            }
        } while (strspn(line, " \t\r\n") == strlen(line));
        int argument_count = sscanf(line, "%31s " FORMAT_ARG " " FORMAT_ARG " " FORMAT_ARG " " FORMAT_ARG " %c", opcode, &instruction->arg_0, &instruction->arg_1, &instruction->arg_2, &instruction->arg_3, &extra);
        if (argument_count < 1) {
            goto invalid;
        }
        instruction->opcode = get_opcode(opcode);
        switch (instruction->opcode) {
        case CALC:
            if (argument_count != 1) {
                goto invalid;
            }
            break;
        case FREE:
            if (argument_count != 2) {
                goto invalid;
            }
            break;
        case ALLOC:
        case KMALLOC:
            if (argument_count != 3) {
                goto invalid;
            }
            break;
        case READ:
        case WRITE:
            if (argument_count != 4) {
                goto invalid;
            }
            break;
        case COPY_FROM_USER:
        case COPY_TO_USER:
        case DMA_MEMCPY:
            if (argument_count != 5) {
                goto invalid;
            }
            break;
        case SYSCALL:
            if (argument_count < 2 || argument_count > 5) {
                goto invalid;
            }
            break;
        default:
            goto invalid;
        }
    }
    fclose(file);
    return proc;
invalid:
    fprintf(stderr, "Invalid process description: %s\n", path);
    fclose(file);
    exit(1);
}
