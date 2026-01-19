#!/usr/bin/env python3
"""
Simple HTTP server that provides real-time stock quotes using yfinance.
This allows the C++ order book to fetch market data via HTTP requests.
"""

import json
import yfinance as yf
from http.server import HTTPServer, BaseHTTPRequestHandler
from urllib.parse import urlparse, parse_qs
import sys
import threading
import time

# Cache to store recent quotes
quote_cache = {}
cache_lock = threading.Lock()
CACHE_EXPIRY = 2  # seconds

def get_stock_quote(symbol):
    """Fetch current stock quote using yfinance."""
    try:
        ticker = yf.Ticker(symbol)
        
        # Get real-time quote data
        info = ticker.info
        
        # Try to get the most recent price
        current_price = info.get('currentPrice') or info.get('regularMarketPrice') or info.get('previousClose')
        
        if current_price is None:
            # Fallback: get latest price from history
            hist = ticker.history(period='1d', interval='1m')
            if not hist.empty:
                current_price = float(hist['Close'].iloc[-1])
        
        # Get bid/ask from info
        bid = info.get('bid', current_price * 0.9995 if current_price else None)
        ask = info.get('ask', current_price * 1.0005 if current_price else None)
        bid_size = info.get('bidSize', 100)
        ask_size = info.get('askSize', 100)
        
        # Convert to cents for the order book (multiply by 100)
        if current_price and bid and ask:
            return {
                'symbol': symbol,
                'bid_price': int(bid * 100),
                'ask_price': int(ask * 100),
                'bid_size': int(bid_size),
                'ask_size': int(ask_size),
                'last_price': int(current_price * 100),
                'timestamp': int(time.time())
            }
    except Exception as e:
        print(f"Error fetching quote for {symbol}: {e}", file=sys.stderr)
    
    return None

def get_cached_quote(symbol):
    """Get quote from cache or fetch new one."""
    with cache_lock:
        now = time.time()
        
        # Check if we have a recent cached quote
        if symbol in quote_cache:
            cached_data, cached_time = quote_cache[symbol]
            if now - cached_time < CACHE_EXPIRY:
                return cached_data
        
        # Fetch new quote
        quote = get_stock_quote(symbol)
        if quote:
            quote_cache[symbol] = (quote, now)
        return quote

class QuoteHandler(BaseHTTPRequestHandler):
    """HTTP request handler for stock quotes."""
    
    def log_message(self, format, *args):
        """Override to reduce logging."""
        pass  # Silent mode
    
    def do_GET(self):
        """Handle GET requests for stock quotes."""
        parsed = urlparse(self.path)
        
        if parsed.path == '/quote':
            # Parse query parameters
            params = parse_qs(parsed.query)
            symbol = params.get('symbol', [''])[0].upper()
            
            if not symbol:
                self.send_error(400, "Missing symbol parameter")
                return
            
            # Get quote
            quote = get_cached_quote(symbol)
            
            if quote:
                self.send_response(200)
                self.send_header('Content-type', 'application/json')
                self.send_header('Access-Control-Allow-Origin', '*')
                self.end_headers()
                self.wfile.write(json.dumps(quote).encode())
            else:
                self.send_error(404, f"Could not fetch quote for {symbol}")
        
        elif parsed.path == '/health':
            self.send_response(200)
            self.send_header('Content-type', 'text/plain')
            self.end_headers()
            self.wfile.write(b'OK')
        
        else:
            self.send_error(404, "Endpoint not found. Use /quote?symbol=AAPL")

def run_server(port=8080):
    """Start the HTTP server."""
    server_address = ('', port)
    httpd = HTTPServer(server_address, QuoteHandler)
    print(f"YFinance Quote Server starting on port {port}...")
    print(f"Test with: curl http://localhost:{port}/quote?symbol=AAPL")
    print("Press Ctrl+C to stop")
    
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("\nShutting down server...")
        httpd.shutdown()

if __name__ == '__main__':
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8080
    run_server(port)
