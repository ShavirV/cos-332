writeString(int, char const*):
        push    rbp
        mov     rbp, rsp
        sub     rsp, 16
        mov     DWORD PTR [rbp-4], edi
        mov     QWORD PTR [rbp-16], rsi
        mov     rax, QWORD PTR [rbp-16]
        mov     rdi, rax
        call    strlen
        mov     rdx, rax
        mov     rcx, QWORD PTR [rbp-16]
        mov     eax, DWORD PTR [rbp-4]
        mov     rsi, rcx
        mov     edi, eax
        call    write
        nop
        leave
        ret
.LC0:
        .base64 "G1syShtbSAA="
clearScreen(int):
        push    rbp
        mov     rbp, rsp
        sub     rsp, 16
        mov     DWORD PTR [rbp-4], edi
        mov     eax, DWORD PTR [rbp-4]
        mov     esi, OFFSET FLAT:.LC0
        mov     edi, eax
        call    writeString(int, char const*)
        nop
        leave
        ret
.LC1:
        .string "\r\n"
.LC2:
        .string "\b \b"
readLine(int, char*, unsigned long):
        push    rbp
        mov     rbp, rsp
        sub     rsp, 224
        mov     DWORD PTR [rbp-196], edi
        mov     QWORD PTR [rbp-208], rsi
        mov     QWORD PTR [rbp-216], rdx
        mov     QWORD PTR [rbp-8], 0
        mov     rdx, QWORD PTR [rbp-216]
        mov     rax, QWORD PTR [rbp-208]
        mov     esi, 0
        mov     rdi, rax
        call    memset
        jmp     .L4
.L13:
        movzx   eax, BYTE PTR [rbp-25]
        cmp     al, 13
        jne     .L5
        lea     rax, [rbp-192]
        mov     QWORD PTR [rbp-24], rax
        mov     DWORD PTR [rbp-12], 0
        jmp     .L6
.L7:
        mov     rax, QWORD PTR [rbp-24]
        mov     edx, DWORD PTR [rbp-12]
        mov     QWORD PTR [rax+rdx*8], 0
        add     DWORD PTR [rbp-12], 1
.L6:
        cmp     DWORD PTR [rbp-12], 15
        jbe     .L7
        mov     eax, DWORD PTR [rbp-196]
        lea     edx, [rax+63]
        test    eax, eax
        cmovs   eax, edx
        sar     eax, 6
        mov     esi, eax
        movsx   rax, esi
        mov     rax, QWORD PTR [rbp-192+rax*8]
        mov     edx, DWORD PTR [rbp-196]
        and     edx, 63
        mov     edi, 1
        mov     ecx, edx
        sal     rdi, cl
        mov     rdx, rdi
        or      rdx, rax
        movsx   rax, esi
        mov     QWORD PTR [rbp-192+rax*8], rdx
        mov     QWORD PTR [rbp-48], 0
        mov     QWORD PTR [rbp-40], 5000
        mov     eax, DWORD PTR [rbp-196]
        lea     edi, [rax+1]
        lea     rdx, [rbp-48]
        lea     rax, [rbp-192]
        mov     r8, rdx
        mov     ecx, 0
        mov     edx, 0
        mov     rsi, rax
        call    select
        test    eax, eax
        setg    al
        test    al, al
        je      .L8
        lea     rcx, [rbp-49]
        mov     eax, DWORD PTR [rbp-196]
        mov     edx, 1
        mov     rsi, rcx
        mov     edi, eax
        call    read
.L8:
        mov     eax, DWORD PTR [rbp-196]
        mov     edx, 2
        mov     esi, OFFSET FLAT:.LC1
        mov     edi, eax
        call    write
        jmp     .L9
.L5:
        movzx   eax, BYTE PTR [rbp-25]
        cmp     al, 10
        jne     .L10
        mov     eax, DWORD PTR [rbp-196]
        mov     edx, 2
        mov     esi, OFFSET FLAT:.LC1
        mov     edi, eax
        call    write
        jmp     .L9
.L10:
        movzx   eax, BYTE PTR [rbp-25]
        cmp     al, 127
        je      .L11
        movzx   eax, BYTE PTR [rbp-25]
        cmp     al, 8
        jne     .L12
.L11:
        cmp     QWORD PTR [rbp-8], 0
        je      .L12
        sub     QWORD PTR [rbp-8], 1
        mov     eax, DWORD PTR [rbp-196]
        mov     edx, 3
        mov     esi, OFFSET FLAT:.LC2
        mov     edi, eax
        call    write
        jmp     .L4
.L12:
        mov     rax, QWORD PTR [rbp-216]
        sub     rax, 1
        cmp     QWORD PTR [rbp-8], rax
        jnb     .L4
        movzx   edx, BYTE PTR [rbp-25]
        mov     rsi, QWORD PTR [rbp-208]
        mov     rax, QWORD PTR [rbp-8]
        lea     rcx, [rax+1]
        mov     QWORD PTR [rbp-8], rcx
        add     rax, rsi
        mov     BYTE PTR [rax], dl
        lea     rcx, [rbp-25]
        mov     eax, DWORD PTR [rbp-196]
        mov     edx, 1
        mov     rsi, rcx
        mov     edi, eax
        call    write
.L4:
        lea     rcx, [rbp-25]
        mov     eax, DWORD PTR [rbp-196]
        mov     edx, 1
        mov     rsi, rcx
        mov     edi, eax
        call    read
        test    rax, rax
        setg    al
        test    al, al
        jne     .L13
.L9:
        mov     rdx, QWORD PTR [rbp-208]
        mov     rax, QWORD PTR [rbp-8]
        add     rax, rdx
        mov     BYTE PTR [rax], 0
        nop
        leave
        ret
.LC3:
        .string "\033[33m  > \033[37m"
.LC4:
        .string "\033[0m"
prompt(int, char const*):
        push    rbp
        mov     rbp, rsp
        sub     rsp, 16
        mov     DWORD PTR [rbp-4], edi
        mov     QWORD PTR [rbp-16], rsi
        mov     eax, DWORD PTR [rbp-4]
        mov     esi, OFFSET FLAT:.LC3
        mov     edi, eax
        call    writeString(int, char const*)
        mov     rdx, QWORD PTR [rbp-16]
        mov     eax, DWORD PTR [rbp-4]
        mov     rsi, rdx
        mov     edi, eax
        call    writeString(int, char const*)
        mov     eax, DWORD PTR [rbp-4]
        mov     esi, OFFSET FLAT:.LC4
        mov     edi, eax
        call    writeString(int, char const*)
        nop
        leave
        ret
.LC5:
        .string "\r\n  \033[36m[enter to continue]\033[0m "
waitEnter(int):
        push    rbp
        mov     rbp, rsp
        sub     rsp, 32
        mov     DWORD PTR [rbp-20], edi
        mov     eax, DWORD PTR [rbp-20]
        mov     esi, OFFSET FLAT:.LC5
        mov     edi, eax
        call    writeString(int, char const*)
        lea     rcx, [rbp-4]
        mov     eax, DWORD PTR [rbp-20]
        mov     edx, 4
        mov     rsi, rcx
        mov     edi, eax
        call    readLine(int, char*, unsigned long)
        nop
        leave
        ret
.LC6:
        .string "rb"
.LC7:
        .string "appointments.db"
countAppointments():
        push    rbp
        mov     rbp, rsp
        sub     rsp, 16
        mov     esi, OFFSET FLAT:.LC6
        mov     edi, OFFSET FLAT:.LC7
        call    fopen
        mov     QWORD PTR [rbp-8], rax
        cmp     QWORD PTR [rbp-8], 0
        jne     .L17
        mov     eax, 0
        jmp     .L18
.L17:
        mov     rax, QWORD PTR [rbp-8]
        mov     edx, 2
        mov     esi, 0
        mov     rdi, rax
        call    fseek
        mov     rax, QWORD PTR [rbp-8]
        mov     rdi, rax
        call    ftell
        mov     rcx, rax
        movabs  rdx, 485440633518672411
        mov     rax, rcx
        imul    rdx
        mov     rax, rdx
        sar     rax, 2
        sar     rcx, 63
        mov     rdx, rcx
        sub     rax, rdx
        mov     DWORD PTR [rbp-12], eax
        mov     rax, QWORD PTR [rbp-8]
        mov     rdi, rax
        call    fclose
        mov     eax, DWORD PTR [rbp-12]
.L18:
        leave
        ret
.LC8:
        .string "ab"
addAppointment(Appointment*):
        push    rbp
        mov     rbp, rsp
        sub     rsp, 32
        mov     QWORD PTR [rbp-24], rdi
        mov     esi, OFFSET FLAT:.LC8
        mov     edi, OFFSET FLAT:.LC7
        call    fopen
        mov     QWORD PTR [rbp-8], rax
        cmp     QWORD PTR [rbp-8], 0
        je      .L22
        mov     rdx, QWORD PTR [rbp-8]
        mov     rax, QWORD PTR [rbp-24]
        mov     rcx, rdx
        mov     edx, 1
        mov     esi, 152
        mov     rdi, rax
        call    fwrite
        mov     rax, QWORD PTR [rbp-8]
        mov     rdi, rax
        call    fclose
        jmp     .L19
.L22:
        nop
.L19:
        leave
        ret
.LC9:
        .string "  No appointments.\r\n"
.LC10:
        .string "  ID   Date        Time   With                  Note\r\n"
.LC11:
        .string "  ---- ----------  -----  --------------------  ----------\r\n"
.LC12:
        .string "  %-4d %-10s  %-5s  %-20s  %s\r\n"
.LC13:
        .string "  No appointments yet.\r\n"
.LC14:
        .string "  %d appointment(s).\r\n"
listAppointments(int):
        push    rbp
        mov     rbp, rsp
        sub     rsp, 352
        mov     DWORD PTR [rbp-340], edi
        mov     esi, OFFSET FLAT:.LC6
        mov     edi, OFFSET FLAT:.LC7
        call    fopen
        mov     QWORD PTR [rbp-16], rax
        cmp     QWORD PTR [rbp-16], 0
        jne     .L24
        mov     eax, DWORD PTR [rbp-340]
        mov     esi, OFFSET FLAT:.LC9
        mov     edi, eax
        call    writeString(int, char const*)
        jmp     .L23
.L24:
        mov     DWORD PTR [rbp-4], 0
        mov     eax, DWORD PTR [rbp-340]
        mov     esi, OFFSET FLAT:.LC10
        mov     edi, eax
        call    writeString(int, char const*)
        mov     eax, DWORD PTR [rbp-340]
        mov     esi, OFFSET FLAT:.LC11
        mov     edi, eax
        call    writeString(int, char const*)
        jmp     .L26
.L27:
        mov     edx, DWORD PTR [rbp-176]
        lea     rax, [rbp-176]
        lea     rdi, [rax+15]
        lea     rax, [rbp-176]
        lea     rsi, [rax+4]
        lea     rax, [rbp-336]
        lea     rcx, [rbp-176]
        add     rcx, 71
        push    rcx
        lea     rcx, [rbp-176]
        add     rcx, 21
        push    rcx
        mov     r9, rdi
        mov     r8, rsi
        mov     ecx, edx
        mov     edx, OFFSET FLAT:.LC12
        mov     esi, 160
        mov     rdi, rax
        mov     eax, 0
        call    snprintf
        add     rsp, 16
        lea     rdx, [rbp-336]
        mov     eax, DWORD PTR [rbp-340]
        mov     rsi, rdx
        mov     edi, eax
        call    writeString(int, char const*)
        add     DWORD PTR [rbp-4], 1
.L26:
        mov     rdx, QWORD PTR [rbp-16]
        lea     rax, [rbp-176]
        mov     rcx, rdx
        mov     edx, 1
        mov     esi, 152
        mov     rdi, rax
        call    fread
        cmp     rax, 1
        sete    al
        test    al, al
        jne     .L27
        mov     rax, QWORD PTR [rbp-16]
        mov     rdi, rax
        call    fclose
        mov     eax, DWORD PTR [rbp-340]
        mov     esi, OFFSET FLAT:.LC1
        mov     edi, eax
        call    writeString(int, char const*)
        cmp     DWORD PTR [rbp-4], 0
        jne     .L28
        mov     eax, DWORD PTR [rbp-340]
        mov     esi, OFFSET FLAT:.LC13
        mov     edi, eax
        call    writeString(int, char const*)
        jmp     .L23
.L28:
        mov     edx, DWORD PTR [rbp-4]
        lea     rax, [rbp-336]
        mov     ecx, edx
        mov     edx, OFFSET FLAT:.LC14
        mov     esi, 64
        mov     rdi, rax
        mov     eax, 0
        call    snprintf
        lea     rdx, [rbp-336]
        mov     eax, DWORD PTR [rbp-340]
        mov     rsi, rdx
        mov     edi, eax
        call    writeString(int, char const*)
.L23:
        leave
        ret
.LC15:
        .string "  No matches found.\r\n"
.LC16:
        .string "  %d match(es).\r\n"
searchAppointments(int, char const*):
        push    rbp
        mov     rbp, rsp
        sub     rsp, 352
        mov     DWORD PTR [rbp-340], edi
        mov     QWORD PTR [rbp-352], rsi
        mov     esi, OFFSET FLAT:.LC6
        mov     edi, OFFSET FLAT:.LC7
        call    fopen
        mov     QWORD PTR [rbp-16], rax
        cmp     QWORD PTR [rbp-16], 0
        jne     .L32
        mov     eax, DWORD PTR [rbp-340]
        mov     esi, OFFSET FLAT:.LC9
        mov     edi, eax
        call    writeString(int, char const*)
        jmp     .L31
.L32:
        mov     DWORD PTR [rbp-4], 0
        mov     eax, DWORD PTR [rbp-340]
        mov     esi, OFFSET FLAT:.LC10
        mov     edi, eax
        call    writeString(int, char const*)
        mov     eax, DWORD PTR [rbp-340]
        mov     esi, OFFSET FLAT:.LC11
        mov     edi, eax
        call    writeString(int, char const*)
        jmp     .L34
.L35:
        mov     rax, QWORD PTR [rbp-352]
        lea     rdx, [rbp-176]
        add     rdx, 21
        mov     rsi, rax
        mov     rdi, rdx
        call    strstr
        test    rax, rax
        je      .L34
        mov     edx, DWORD PTR [rbp-176]
        lea     rax, [rbp-176]
        lea     rdi, [rax+15]
        lea     rax, [rbp-176]
        lea     rsi, [rax+4]
        lea     rax, [rbp-336]
        lea     rcx, [rbp-176]
        add     rcx, 71
        push    rcx
        lea     rcx, [rbp-176]
        add     rcx, 21
        push    rcx
        mov     r9, rdi
        mov     r8, rsi
        mov     ecx, edx
        mov     edx, OFFSET FLAT:.LC12
        mov     esi, 160
        mov     rdi, rax
        mov     eax, 0
        call    snprintf
        add     rsp, 16
        lea     rdx, [rbp-336]
        mov     eax, DWORD PTR [rbp-340]
        mov     rsi, rdx
        mov     edi, eax
        call    writeString(int, char const*)
        add     DWORD PTR [rbp-4], 1
.L34:
        mov     rdx, QWORD PTR [rbp-16]
        lea     rax, [rbp-176]
        mov     rcx, rdx
        mov     edx, 1
        mov     esi, 152
        mov     rdi, rax
        call    fread
        cmp     rax, 1
        sete    al
        test    al, al
        jne     .L35
        mov     rax, QWORD PTR [rbp-16]
        mov     rdi, rax
        call    fclose
        mov     eax, DWORD PTR [rbp-340]
        mov     esi, OFFSET FLAT:.LC1
        mov     edi, eax
        call    writeString(int, char const*)
        cmp     DWORD PTR [rbp-4], 0
        jne     .L36
        mov     eax, DWORD PTR [rbp-340]
        mov     esi, OFFSET FLAT:.LC15
        mov     edi, eax
        call    writeString(int, char const*)
        jmp     .L31
.L36:
        mov     edx, DWORD PTR [rbp-4]
        lea     rax, [rbp-336]
        mov     ecx, edx
        mov     edx, OFFSET FLAT:.LC16
        mov     esi, 64
        mov     rdi, rax
        mov     eax, 0
        call    snprintf
        lea     rdx, [rbp-336]
        mov     eax, DWORD PTR [rbp-340]
        mov     rsi, rdx
        mov     edi, eax
        call    writeString(int, char const*)
.L31:
        leave
        ret
.LC17:
        .string "wb"
.LC18:
        .string "appointments.tmp"
deleteAppointment(int):
        push    rbp
        mov     rbp, rsp
        sub     rsp, 192
        mov     DWORD PTR [rbp-180], edi
        mov     esi, OFFSET FLAT:.LC6
        mov     edi, OFFSET FLAT:.LC7
        call    fopen
        mov     QWORD PTR [rbp-8], rax
        cmp     QWORD PTR [rbp-8], 0
        je      .L46
        mov     esi, OFFSET FLAT:.LC17
        mov     edi, OFFSET FLAT:.LC18
        call    fopen
        mov     QWORD PTR [rbp-16], rax
        cmp     QWORD PTR [rbp-16], 0
        jne     .L43
        mov     rax, QWORD PTR [rbp-8]
        mov     rdi, rax
        call    fclose
        jmp     .L39
.L44:
        mov     eax, DWORD PTR [rbp-176]
        cmp     DWORD PTR [rbp-180], eax
        je      .L43
        mov     rdx, QWORD PTR [rbp-16]
        lea     rax, [rbp-176]
        mov     rcx, rdx
        mov     edx, 1
        mov     esi, 152
        mov     rdi, rax
        call    fwrite
.L43:
        mov     rdx, QWORD PTR [rbp-8]
        lea     rax, [rbp-176]
        mov     rcx, rdx
        mov     edx, 1
        mov     esi, 152
        mov     rdi, rax
        call    fread
        cmp     rax, 1
        sete    al
        test    al, al
        jne     .L44
        mov     rax, QWORD PTR [rbp-8]
        mov     rdi, rax
        call    fclose
        mov     rax, QWORD PTR [rbp-16]
        mov     rdi, rax
        call    fclose
        mov     esi, OFFSET FLAT:.LC7
        mov     edi, OFFSET FLAT:.LC18
        call    rename
        jmp     .L39
.L46:
        nop
.L39:
        leave
        ret
.LC19:
        .string "\r\n  \033[36m\033[1m-- Add Appointment --\r\n\r\n\033[0m"
.LC20:
        .string "Date (YYYY-MM-DD): "
.LC21:
        .string "Time (HH:MM):      "
.LC22:
        .string "With:              "
.LC23:
        .string "Note (optional):   "
.LC24:
        .string "\r\n  \033[32mSaved.\033[0m"
menuAdd(int):
        push    rbp
        mov     rbp, rsp
        sub     rsp, 176
        mov     DWORD PTR [rbp-164], edi
        lea     rax, [rbp-160]
        mov     edx, 152
        mov     esi, 0
        mov     rdi, rax
        call    memset
        call    countAppointments()
        add     eax, 1
        mov     DWORD PTR [rbp-160], eax
        mov     eax, DWORD PTR [rbp-164]
        mov     edi, eax
        call    clearScreen(int)
        mov     eax, DWORD PTR [rbp-164]
        mov     esi, OFFSET FLAT:.LC19
        mov     edi, eax
        call    writeString(int, char const*)
        mov     eax, DWORD PTR [rbp-164]
        mov     esi, OFFSET FLAT:.LC20
        mov     edi, eax
        call    prompt(int, char const*)
        lea     rax, [rbp-160]
        lea     rcx, [rax+4]
        mov     eax, DWORD PTR [rbp-164]
        mov     edx, 11
        mov     rsi, rcx
        mov     edi, eax
        call    readLine(int, char*, unsigned long)
        mov     eax, DWORD PTR [rbp-164]
        mov     esi, OFFSET FLAT:.LC21
        mov     edi, eax
        call    prompt(int, char const*)
        lea     rax, [rbp-160]
        lea     rcx, [rax+15]
        mov     eax, DWORD PTR [rbp-164]
        mov     edx, 6
        mov     rsi, rcx
        mov     edi, eax
        call    readLine(int, char*, unsigned long)
        mov     eax, DWORD PTR [rbp-164]
        mov     esi, OFFSET FLAT:.LC22
        mov     edi, eax
        call    prompt(int, char const*)
        lea     rax, [rbp-160]
        lea     rcx, [rax+21]
        mov     eax, DWORD PTR [rbp-164]
        mov     edx, 50
        mov     rsi, rcx
        mov     edi, eax
        call    readLine(int, char*, unsigned long)
        mov     eax, DWORD PTR [rbp-164]
        mov     esi, OFFSET FLAT:.LC23
        mov     edi, eax
        call    prompt(int, char const*)
        lea     rax, [rbp-160]
        lea     rcx, [rax+71]
        mov     eax, DWORD PTR [rbp-164]
        mov     edx, 80
        mov     rsi, rcx
        mov     edi, eax
        call    readLine(int, char*, unsigned long)
        lea     rax, [rbp-160]
        mov     rdi, rax
        call    addAppointment(Appointment*)
        mov     eax, DWORD PTR [rbp-164]
        mov     esi, OFFSET FLAT:.LC24
        mov     edi, eax
        call    writeString(int, char const*)
        mov     eax, DWORD PTR [rbp-164]
        mov     edi, eax
        call    waitEnter(int)
        nop
        leave
        ret
.LC25:
        .string "\r\n  \033[36m\033[1m-- Appointments --\r\n\r\n\033[0m"
menuList(int):
        push    rbp
        mov     rbp, rsp
        sub     rsp, 16
        mov     DWORD PTR [rbp-4], edi
        mov     eax, DWORD PTR [rbp-4]
        mov     edi, eax
        call    clearScreen(int)
        mov     eax, DWORD PTR [rbp-4]
        mov     esi, OFFSET FLAT:.LC25
        mov     edi, eax
        call    writeString(int, char const*)
        mov     eax, DWORD PTR [rbp-4]
        mov     edi, eax
        call    listAppointments(int)
        mov     eax, DWORD PTR [rbp-4]
        mov     edi, eax
        call    waitEnter(int)
        nop
        leave
        ret
.LC26:
        .string "\r\n  \033[36m\033[1m-- Search --\r\n\r\n\033[0m"
.LC27:
        .string "Search 'with' field: "
menuSearch(int):
        push    rbp
        mov     rbp, rsp
        sub     rsp, 80
        mov     DWORD PTR [rbp-68], edi
        mov     eax, DWORD PTR [rbp-68]
        mov     edi, eax
        call    clearScreen(int)
        mov     eax, DWORD PTR [rbp-68]
        mov     esi, OFFSET FLAT:.LC26
        mov     edi, eax
        call    writeString(int, char const*)
        mov     eax, DWORD PTR [rbp-68]
        mov     esi, OFFSET FLAT:.LC27
        mov     edi, eax
        call    prompt(int, char const*)
        lea     rcx, [rbp-64]
        mov     eax, DWORD PTR [rbp-68]
        mov     edx, 50
        mov     rsi, rcx
        mov     edi, eax
        call    readLine(int, char*, unsigned long)
        mov     eax, DWORD PTR [rbp-68]
        mov     esi, OFFSET FLAT:.LC1
        mov     edi, eax
        call    writeString(int, char const*)
        lea     rdx, [rbp-64]
        mov     eax, DWORD PTR [rbp-68]
        mov     rsi, rdx
        mov     edi, eax
        call    searchAppointments(int, char const*)
        mov     eax, DWORD PTR [rbp-68]
        mov     edi, eax
        call    waitEnter(int)
        nop
        leave
        ret
.LC28:
        .string "\r\n  \033[36m\033[1m-- Delete Appointment --\r\n\r\n\033[0m"
.LC29:
        .string "ID to delete (0 = cancel): "
.LC30:
        .string "  \033[33mCancelled.\r\n\033[0m"
.LC31:
        .string "  \033[32mDeleted.\r\n\033[0m"
menuDelete(int):
        push    rbp
        mov     rbp, rsp
        sub     rsp, 48
        mov     DWORD PTR [rbp-36], edi
        mov     eax, DWORD PTR [rbp-36]
        mov     edi, eax
        call    clearScreen(int)
        mov     eax, DWORD PTR [rbp-36]
        mov     esi, OFFSET FLAT:.LC28
        mov     edi, eax
        call    writeString(int, char const*)
        mov     eax, DWORD PTR [rbp-36]
        mov     edi, eax
        call    listAppointments(int)
        mov     eax, DWORD PTR [rbp-36]
        mov     esi, OFFSET FLAT:.LC1
        mov     edi, eax
        call    writeString(int, char const*)
        mov     eax, DWORD PTR [rbp-36]
        mov     esi, OFFSET FLAT:.LC29
        mov     edi, eax
        call    prompt(int, char const*)
        lea     rcx, [rbp-32]
        mov     eax, DWORD PTR [rbp-36]
        mov     edx, 16
        mov     rsi, rcx
        mov     edi, eax
        call    readLine(int, char*, unsigned long)
        lea     rax, [rbp-32]
        mov     rdi, rax
        call    atoi
        mov     DWORD PTR [rbp-4], eax
        cmp     DWORD PTR [rbp-4], 0
        jg      .L51
        mov     eax, DWORD PTR [rbp-36]
        mov     esi, OFFSET FLAT:.LC30
        mov     edi, eax
        call    writeString(int, char const*)
        jmp     .L52
.L51:
        mov     eax, DWORD PTR [rbp-4]
        mov     edi, eax
        call    deleteAppointment(int)
        mov     eax, DWORD PTR [rbp-36]
        mov     esi, OFFSET FLAT:.LC31
        mov     edi, eax
        call    writeString(int, char const*)
.L52:
        mov     eax, DWORD PTR [rbp-36]
        mov     edi, eax
        call    waitEnter(int)
        nop
        leave
        ret
.LC32:
        .string "\033[36m\033[1m\r\n    +----------------------------------+\r\n    |      APPOINTMENT MANAGER         |\r\n    +----------------------------------+\r\n\r\n\033[0m"
.LC33:
        .string "     \033[32m\033[1m1\033[0m\033[37m  Add appointment\r\n     \033[32m\033[1m2\033[0m\033[37m  List appointments\r\n     \033[32m\033[1m3\033[0m\033[37m  Search appointments\r\n     \033[31m\033[1m4\033[0m\033[37m  Delete appointment\r\n     \033[31m\033[1m5\033[0m\033[37m  Quit\r\n\r\n\033[0m"
.LC34:
        .string "Command: "
.LC35:
        .base64 "DQogIBtbMzZtR29vZGJ5ZS4NCg0KG1swbQA="
handleClient(int):
        push    rbp
        mov     rbp, rsp
        sub     rsp, 32
        mov     DWORD PTR [rbp-20], edi
.L60:
        mov     eax, DWORD PTR [rbp-20]
        mov     edi, eax
        call    clearScreen(int)
        mov     eax, DWORD PTR [rbp-20]
        mov     esi, OFFSET FLAT:.LC32
        mov     edi, eax
        call    writeString(int, char const*)
        mov     eax, DWORD PTR [rbp-20]
        mov     esi, OFFSET FLAT:.LC33
        mov     edi, eax
        call    writeString(int, char const*)
        mov     eax, DWORD PTR [rbp-20]
        mov     esi, OFFSET FLAT:.LC34
        mov     edi, eax
        call    prompt(int, char const*)
        lea     rcx, [rbp-4]
        mov     eax, DWORD PTR [rbp-20]
        mov     edx, 4
        mov     rsi, rcx
        mov     edi, eax
        call    readLine(int, char*, unsigned long)
        movzx   eax, BYTE PTR [rbp-4]
        cmp     al, 49
        jne     .L54
        mov     eax, DWORD PTR [rbp-20]
        mov     edi, eax
        call    menuAdd(int)
        jmp     .L60
.L54:
        movzx   eax, BYTE PTR [rbp-4]
        cmp     al, 50
        jne     .L56
        mov     eax, DWORD PTR [rbp-20]
        mov     edi, eax
        call    menuList(int)
        jmp     .L60
.L56:
        movzx   eax, BYTE PTR [rbp-4]
        cmp     al, 51
        jne     .L57
        mov     eax, DWORD PTR [rbp-20]
        mov     edi, eax
        call    menuSearch(int)
        jmp     .L60
.L57:
        movzx   eax, BYTE PTR [rbp-4]
        cmp     al, 52
        jne     .L58
        mov     eax, DWORD PTR [rbp-20]
        mov     edi, eax
        call    menuDelete(int)
        jmp     .L60
.L58:
        movzx   eax, BYTE PTR [rbp-4]
        cmp     al, 53
        je      .L62
        jmp     .L60
.L62:
        nop
        mov     eax, DWORD PTR [rbp-20]
        mov     edi, eax
        call    clearScreen(int)
        mov     eax, DWORD PTR [rbp-20]
        mov     esi, OFFSET FLAT:.LC35
        mov     edi, eax
        call    writeString(int, char const*)
        mov     eax, DWORD PTR [rbp-20]
        mov     edi, eax
        call    close
        mov     edi, 0
        call    exit
.LC36:
        .string "socket"
.LC37:
        .string "bind"
.LC38:
        .string "listen"
.LC39:
        .string "Listening on port %d\n"
main:
        push    rbp
        mov     rbp, rsp
        sub     rsp, 48
        mov     DWORD PTR [rbp-36], 1
        mov     edx, 0
        mov     esi, 1
        mov     edi, 2
        call    socket
        mov     DWORD PTR [rbp-4], eax
        cmp     DWORD PTR [rbp-4], 0
        jns     .L64
        mov     edi, OFFSET FLAT:.LC36
        call    perror
        mov     eax, 1
        jmp     .L72
.L64:
        lea     rdx, [rbp-36]
        mov     eax, DWORD PTR [rbp-4]
        mov     r8d, 4
        mov     rcx, rdx
        mov     edx, 2
        mov     esi, 1
        mov     edi, eax
        call    setsockopt
        lea     rax, [rbp-32]
        mov     edx, 16
        mov     esi, 0
        mov     rdi, rax
        call    memset
        mov     WORD PTR [rbp-32], 2
        mov     DWORD PTR [rbp-28], 0
        mov     edi, 5555
        call    htons
        mov     WORD PTR [rbp-30], ax
        lea     rcx, [rbp-32]
        mov     eax, DWORD PTR [rbp-4]
        mov     edx, 16
        mov     rsi, rcx
        mov     edi, eax
        call    bind
        shr     eax, 31
        test    al, al
        je      .L66
        mov     edi, OFFSET FLAT:.LC37
        call    perror
        mov     eax, 1
        jmp     .L72
.L66:
        mov     eax, DWORD PTR [rbp-4]
        mov     esi, 10
        mov     edi, eax
        call    listen
        shr     eax, 31
        test    al, al
        je      .L67
        mov     edi, OFFSET FLAT:.LC38
        call    perror
        mov     eax, 1
        jmp     .L72
.L67:
        mov     esi, 5555
        mov     edi, OFFSET FLAT:.LC39
        mov     eax, 0
        call    printf
        mov     rax, QWORD PTR stdout[rip]
        mov     rdi, rax
        call    fflush
.L71:
        mov     eax, DWORD PTR [rbp-4]
        mov     edx, 0
        mov     esi, 0
        mov     edi, eax
        call    accept
        mov     DWORD PTR [rbp-8], eax
        cmp     DWORD PTR [rbp-8], 0
        js      .L73
        call    fork
        test    eax, eax
        sete    al
        test    al, al
        je      .L70
        mov     eax, DWORD PTR [rbp-4]
        mov     edi, eax
        call    close
        mov     eax, DWORD PTR [rbp-8]
        mov     edi, eax
        call    handleClient(int)
.L70:
        mov     eax, DWORD PTR [rbp-8]
        mov     edi, eax
        call    close
        jmp     .L71
.L73:
        nop
        jmp     .L71
.L72:
        leave
        ret
