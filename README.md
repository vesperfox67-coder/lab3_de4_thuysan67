# LAB 3 -- ĐỀ 4: GIÁM SÁT MÔI TRƯỜNG BỂ NUÔI THỦY, HẢI SẢN

**Họ tên:** Triệu Trọng Hiếu\
**MSV:** 231432235\
**Lớp:** KTVT K64

## Liên kết

-   **Link project Wokwi:**
    https://wokwi.com/projects/480197337814151169
-   **Website giám sát trực tuyến (GitHub Pages):**
    https://vesperfox67-coder.github.io/lab3_de4_thuysan67/
-   **Video demo:**
-   **Báo cáo:** `TrieuTrongHieu_231432235_lab3_de4.docx`

## Giới thiệu

Repository lưu trữ các file của bài Lab 3 -- Đề 4: xây dựng hệ thống tự
động giám sát thông số môi trường cho bể nuôi thủy, hải sản sử dụng
ESP32, FreeRTOS và MQTT.

Hệ thống đọc mực nước bằng cảm biến HC-SR04, đo nhiệt độ bằng DS18B20,
gửi dữ liệu qua MQTT và hiển thị trên website. Người dùng có thể theo
dõi thông số và điều khiển máy bơm nước vào, van thoát nước ra ở chế độ
thủ công; hệ thống cũng hỗ trợ điều khiển tự động theo mực nước.

## Nội dung repository

  ------------------------------------------------------------------------------
  Tệp                                       Mô tả
  --------------------------------------- --------------------------------------
  `TrieuTrongHieu_231432235_lab3_de4.docx`  Báo cáo thực hiện Lab 3 -- Đề 4

  `index.html`                              Giao diện website giám sát và điều
                                            khiển

  `sketch.ino`                              Chương trình ESP32 và các Task
                                            FreeRTOS

  `diagram.json`                            Sơ đồ mạch mô phỏng trên Wokwi

  `libraries.txt`                           Danh sách thư viện sử dụng
  ------------------------------------------------------------------------------

## Công nghệ sử dụng

-   ESP32 và FreeRTOS
-   Cảm biến HC-SR04, DS18B20
-   MQTT -- HiveMQ Public Broker
-   HTML, CSS, JavaScript, MQTT.js và Chart.js
-   Wokwi và GitHub Pages

------------------------------------------------------------------------

**Bài thực hiện:** Lab 3 -- Đề 4 \| **Triệu Trọng Hiếu -- KTVT K64**
