/**
 * @file dns_server.c
 * @brief DNS server implementation for captive portal
 * 
 * Implements a simple DNS server that responds to all queries
 * with the AP's IP address. This ensures all HTTP requests
 * from connected clients are directed to the captive portal.
 */

#include "dns_server.h"
#include "dlm_config.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "lwip/sockets.h"
#include "lwip/ip_addr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "dns_server";

/* ============== DNS Protocol Definitions ============== */

#define DNS_PORT            53
#define DNS_MAX_PACKET_SIZE 512

/* DNS header flags */
#define DNS_FLAG_RESPONSE   0x8000
#define DNS_FLAG_AUTHORITATIVE 0x0400

/* DNS response codes */
#define DNS_RCODE_NOERROR   0
#define DNS_RCODE_NXDOMAIN  3

/* DNS record types */
#define DNS_TYPE_A          1   /* IPv4 address */
#define DNS_TYPE_AAAA       28  /* IPv6 address */

/* DNS classes */
#define DNS_CLASS_IN        1

typedef struct __attribute__((packed)) {
    uint16_t id;        /* Transaction ID */
    uint16_t flags;     /* Flags */
    uint16_t questions; /* Number of questions */
    uint16_t answers;   /* Number of answer RRs */
    uint16_t authority; /* Number of authority RRs */
    uint16_t additional;/* Number of additional RRs */
} dns_header_t;

/* ============== Server State ============== */

static struct {
    bool running;
    int socket_fd;
    TaskHandle_t task_handle;
    uint32_t server_ip; /* IP to return for all queries */
} s_dns = {0};

/* ============== DNS Packet Processing ============== */

static int parse_dns_name(const uint8_t *packet, size_t packet_len,
                          size_t offset, char *name, size_t name_len)
{
    size_t name_pos = 0;
    size_t pos = offset;
    bool first = true;
    
    while (pos < packet_len) {
        uint8_t len = packet[pos++];
        
        if (len == 0) {
            /* End of name */
            break;
        }
        
        if (len & 0xC0) {
            /* Compression pointer - not implemented */
            pos++;  /* Skip pointer */
            break;
        }
        
        if (pos + len > packet_len) {
            return -1;  /* Invalid */
        }
        
        if (!first) {
            if (name_pos < name_len - 1) {
                name[name_pos++] = '.';
            }
        }
        first = false;
        
        for (int i = 0; i < len && name_pos < name_len - 1; i++) {
            name[name_pos++] = packet[pos++];
        }
    }
    
    name[name_pos] = '\0';
    return (int)pos;
}

static void process_dns_request(const uint8_t *req, size_t req_len,
                                 uint8_t *resp, size_t *resp_len,
                                 uint32_t server_ip)
{
    if (req_len < sizeof(dns_header_t)) {
        *resp_len = 0;
        return;
    }
    
    dns_header_t *req_header = (dns_header_t *)req;
    dns_header_t *resp_header = (dns_header_t *)resp;
    
    /* Copy header and modify for response */
    memcpy(resp_header, req_header, sizeof(dns_header_t));
    resp_header->flags = ntohs(req_header->flags) | DNS_FLAG_RESPONSE;
    resp_header->flags |= DNS_FLAG_AUTHORITATIVE;
    resp_header->flags = htons(resp_header->flags);
    
    uint16_t questions = ntohs(req_header->questions);
    resp_header->answers = htons(questions);
    resp_header->authority = 0;
    resp_header->additional = 0;
    
    size_t pos = sizeof(dns_header_t);
    size_t resp_pos = sizeof(dns_header_t);
    
    /* Process each question */
    for (int q = 0; q < questions && pos < req_len; q++) {
        char name[256];
        int name_len = parse_dns_name(req, req_len, pos, name, sizeof(name));
        if (name_len < 0) break;
        
        /* Copy question section to response */
        size_t qsize = name_len - pos + 4;  /* Name + QTYPE + QCLASS */
        if (resp_pos + qsize > DNS_MAX_PACKET_SIZE) break;
        memcpy(&resp[resp_pos], &req[pos], qsize);
        resp_pos += qsize;
        
        /* Read QTYPE and QCLASS */
        uint16_t qtype = (req[name_len] << 8) | req[name_len + 1];
        uint16_t qclass = (req[name_len + 2] << 8) | req[name_len + 3];
        
        pos = name_len + 4;
        
        /* Only answer A or AAAA queries */
        if ((qtype == DNS_TYPE_A || qtype == DNS_TYPE_AAAA) && 
            qclass == DNS_CLASS_IN) {
            
            /* Add answer section */
            /* Name pointer to question */
            if (resp_pos + 12 > DNS_MAX_PACKET_SIZE) break;
            resp[resp_pos++] = 0xC0;
            resp[resp_pos++] = sizeof(dns_header_t);
            
            /* Type A */
            resp[resp_pos++] = 0;
            resp[resp_pos++] = DNS_TYPE_A;
            
            /* Class IN */
            resp[resp_pos++] = 0;
            resp[resp_pos++] = DNS_CLASS_IN;
            
            /* TTL: 5 minutes (300 seconds = 0x0000012C) */
            resp[resp_pos++] = 0;
            resp[resp_pos++] = 0;
            resp[resp_pos++] = 1;
            resp[resp_pos++] = 44;
            
            /* RDLENGTH: 4 bytes for IPv4 */
            resp[resp_pos++] = 0;
            resp[resp_pos++] = 4;
            
            /* RDATA: Our IP address */
            resp[resp_pos++] = (server_ip >> 0) & 0xFF;
            resp[resp_pos++] = (server_ip >> 8) & 0xFF;
            resp[resp_pos++] = (server_ip >> 16) & 0xFF;
            resp[resp_pos++] = (server_ip >> 24) & 0xFF;
        }
    }
    
    *resp_len = resp_pos;
}

/* ============== Server Task ============== */

static void dns_server_task(void *pvParameters)
{
    (void)pvParameters;
    
    uint8_t rx_buffer[DNS_MAX_PACKET_SIZE];
    uint8_t tx_buffer[DNS_MAX_PACKET_SIZE];
    
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    
    ESP_LOGI(TAG, "DNS server started on port %d", DNS_PORT);
    
    while (s_dns.running) {
        ssize_t len = recvfrom(s_dns.socket_fd, rx_buffer, sizeof(rx_buffer), 0,
                               (struct sockaddr *)&client_addr, &client_len);
        
        if (len < 0) {
            if (s_dns.running) {
                ESP_LOGE(TAG, "recvfrom failed: errno %d", errno);
                vTaskDelay(pdMS_TO_TICKS(100));
            }
            continue;
        }
        
        /* Process request and generate response */
        size_t resp_len;
        process_dns_request(rx_buffer, len, tx_buffer, &resp_len, s_dns.server_ip);
        
        if (resp_len > 0) {
            /* Send response */
            ssize_t sent = sendto(s_dns.socket_fd, tx_buffer, resp_len, 0,
                                  (struct sockaddr *)&client_addr, client_len);
            if (sent < 0) {
                ESP_LOGW(TAG, "sendto failed: errno %d", errno);
            }
        }
    }
    
    ESP_LOGI(TAG, "DNS server stopped");
    s_dns.task_handle = NULL;
    vTaskDelete(NULL);
}

/* ============== Public API ============== */

esp_err_t dns_server_start(const char *ip_addr)
{
    if (s_dns.running) {
        return ESP_OK;
    }
    
    /* Parse IP address */
    ip_addr_t addr;
    if (!ipaddr_aton(ip_addr, &addr)) {
        ESP_LOGE(TAG, "Invalid IP address: %s", ip_addr);
        return ESP_ERR_INVALID_ARG;
    }
    s_dns.server_ip = ip_addr_get_ip4_u32(&addr);
    
    /* Create UDP socket */
    s_dns.socket_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s_dns.socket_fd < 0) {
        ESP_LOGE(TAG, "Failed to create socket: errno %d", errno);
        return ESP_FAIL;
    }
    
    /* Bind to DNS port */
    struct sockaddr_in server_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(DNS_PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    
    if (bind(s_dns.socket_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        ESP_LOGE(TAG, "Failed to bind: errno %d", errno);
        close(s_dns.socket_fd);
        s_dns.socket_fd = -1;
        return ESP_FAIL;
    }
    
    /* Start server task */
    s_dns.running = true;
    BaseType_t ret = xTaskCreate(
        dns_server_task,
        "dns_server",
        3072,
        NULL,
        5,
        &s_dns.task_handle
    );
    
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create DNS task");
        close(s_dns.socket_fd);
        s_dns.socket_fd = -1;
        s_dns.running = false;
        return ESP_ERR_NO_MEM;
    }
    
    ESP_LOGI(TAG, "DNS server started, resolving all queries to %s", ip_addr);
    return ESP_OK;
}

void dns_server_stop(void)
{
    if (!s_dns.running) {
        return;
    }
    
    s_dns.running = false;
    
    /* Close socket to unblock recvfrom */
    if (s_dns.socket_fd >= 0) {
        close(s_dns.socket_fd);
        s_dns.socket_fd = -1;
    }
    
    /* Wait for task to finish */
    if (s_dns.task_handle != NULL) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    ESP_LOGI(TAG, "DNS server stopped");
}

bool dns_server_is_running(void)
{
    return s_dns.running;
}
