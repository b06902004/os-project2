OS Project 2 Report
======

Programming Design
------------------
### fcntl 實作
user program 到 device 之間的 I/O 簡單地使用一個大小為`BUF_SIZE` 的 buffer 來回將檔案傳送至裝置（或反過來）。而 device 之間的 I/O 則利用 sample code 提供的 ksocket 完成。
### mmap 實作
user program 存取 input/output 檔案的實作方式即為呼叫系統已經提供的函式 `mmap`。
而 user program 與 device 溝通的實作方式以下列出主要幾點：
1. 為 device 的 file_operations 的 `.mmap` 提供自己定義的函式。
2. 對於 device 所對應的 `struct file` 中的欄位`private_data` 透過`kmalloc`配置一塊記憶體。
3. 結合第一與第二點，當 user program 呼叫 `mmap`，即可將 user 的一塊虛擬記憶體對應到 device 的 `private data` 所指向的記憶體區塊，這使得 user program 與 device 能夠共享一塊記憶體。
4. 另外，也要有方式讓 user program 能夠要求 device 對共享記憶體進行操作（e.g., master device 該寫出資料給 slave device），這方面可以透過 `ioctl` 的自定義選項來命令。

Result
----------
#### master method : fcntl, slave method : fcntl
File: file1_in \
master: Transmission time: 3.932111 ms, File size: 32 bytes \
slave: Transmission time: 3.363558 ms, File size: 32 bytes

File: file2_in \
master: Transmission time: 20.983933 ms, File size: 4619 bytes \
slave: Transmission time: 26.345381 ms, File size: 4619 bytes

File: file3_in \
master: Transmission time: 27.867186 ms, File size: 77566 bytes \
slave: Transmission time: 33.552561 ms, File size: 77566 bytes

File: file4_in \
master: Transmission time: 2709.906936 ms, File size: 12022885 bytes \
slave: Transmission time: 2716.130047 ms, File size: 12022885 bytes

#### master method : fcntl, slave method : mmap
File: file1_in \
master: Transmission time: 0.943761 ms, File size: 32 bytes \
slave: Transmission time: 1.146744 ms, File size: 32 bytes

File: file2_in \
master: Transmission time: 1.798028 ms, File size: 4619 bytes \
slave: Transmission time: 1.939612 ms, File size: 4619 bytes

File: file3_in \
master: Transmission time: 10.459119 ms, File size: 77566 bytes \
slave: Transmission time: 12.460376 ms, File size: 77566 bytes

File: file4_in \
master: Transmission time: 2235.666199 ms, File size: 12022885 bytes \
slave: Transmission time: 2257.261061 ms, File size: 12022885 bytes

#### master method : mmap, slave method : fcntl
File: file1_in \
master: Transmission time: 0.969352 ms, File size: 32 bytes \
slave: Transmission time: 2.081432 ms, File size: 32 bytes

File: file2_in \
master: Transmission time: 0.951022 ms, File size: 4619 bytes \
slave: Transmission time: 1.987701 ms, File size: 4619 bytes

File: file3_in \
master: Transmission time: 0.960035 ms, File size: 77566 bytes \
slave: Transmission time: 11.743994 ms, File size: 77566 bytes

File: file4_in \
master: Transmission time: 759.945882 ms, File size: 12022885 bytes \
slave: Transmission time: 1235.788843 ms, File size: 12022885 bytes

#### master method : mmap, slave method : mmap
File: file1_in \
master: Transmission time: 0.928425 ms, File size: 32 bytes \
slave: Transmission time: 2.053713 ms, File size: 32 bytes

File: file2_in \
master: Transmission time: 1.088437 ms, File size: 4619 bytes \
slave: Transmission time: 1.775639 ms, File size: 4619 bytes

File: file3_in \
master: Transmission time: 1.156033 ms, File size: 77566 bytes \
slave: Transmission time: 8.177147 ms, File size: 77566 bytes

File: file4_in \
master: Transmission time: 604.089042 ms, File size: 12022885 bytes \
slave: Transmission time: 845.916623 ms, File size: 12022885 bytes

Comparison
----------
以下我們只比較大檔案（file4_in）的不同 I/O 之間的表現，因為當 input file 的 size 很小的時候，開檔案、關檔案等等的 overhead 會占大部分的比率，使得結果僅有些微差異。

注：(mmap, fcntl) 代表 master 使用 method "mmap"，且 slave 使用 method "fcntl"，以此類推。
1. (mmap, mmap) vs. (fcntl, fcntl) \
從上面的結果發現，(mmap, mmap) 所花的時間比 (fcntl, fcntl) 快將近四倍，這顯示了當檔案很大時，使用 mmap 當作 I/O 方式的效率比較好。主要原因有兩點：(a.) 使用一般 I/O 時，每次呼叫 write()/read() 就是呼叫一次 system call，而 system call 的呼叫比起一般函式需要相當大的 overhead。相對地，使用 mmap 時會大量減少呼叫 system call 的次數，因此節省不少時間。(b.) 當呼叫 write()/read() 時，實際上執行的函式個別是 send_msg() 與 receive_msg()，而它們當中會需要將 user space 的資料複製到 kernel space，造成相當大的額外花費。相反地，當使用 mmap() 時，kernel 與 user program 能夠共享同一塊記憶體區域，因此不需要複製貼上。
    
2. (mmap, fcntl) vs. (fcntl, mmap) \
從上面的結果發現，(mmap, fcntl) 所花的時間是 759.945882 ms，而 (fcntl mmap) 花的時間卻是 2235.666199 ms，這代表 slave 端使用 mmap 不會有太大的優化。原因主要來自 master 端若是依然使用較慢的一般 I/O，slave 端就算有較快的 I/O，也會被迫等待 master 端傳來資料，使得整體時間被拉長。

Work list
---------
我們的小組開了兩次聚會來完成這次 project。
#### 李辰暘 b06902004
參與兩次聚會。
#### 高為勳 b06902116
參與兩次聚會。
#### 張凱華 b06902050
參與一次聚會。
#### 劉紹凱 b06902134
參與一次聚會。
#### 李學翰 b06902108
參與零次聚會。
