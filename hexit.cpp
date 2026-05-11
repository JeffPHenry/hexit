/******************************\
 
 hexit.cpp
 140224
 
 HexIt is a command line hex
 viewer and editor.
 
 \******************************/

#include "hexit.h"

#include <algorithm>
#include <cstdlib>
#include <unistd.h>

const char HEX_NIBBLE[0x10] =
{
	'0', '1', '2', '3',
	'4', '5', '6', '7',
	'8', '9', 'A', 'B',
	'C', 'D', 'E', 'F'
};

uint g_column_pos[0x10] =
{
     0,  2,  5,  7,
	10, 12, 15, 17,
	20, 22, 25, 27,
	30, 32, 35, 37
};
#define COLUMN_POS(x) ((m_bShowByteCount?10:0)+g_column_pos[x])

HexIt::HexIt()
:	m_pFile(NULL)
,	m_bRunning(false)
,	m_bBufferDirty(false)
,	m_uHeight(40)
,	m_uWidth(80)
,	m_uFilePos(0)
,	m_uFileSize(0)
,	m_bShowColor(false)
,	m_bPrintUpper(false)
,	m_bShowByteCount(true)
,	m_bShowASCII(true)
,	m_clipboardByte(0)
,	m_hasClipboard(false)
,	m_selAnchor(-1)
{
    snprintf(m_appVersion, sizeof(m_appVersion), "HexIt v%d.%d", VERSION_MAJOR, VERSION_MINOR);
}

HexIt::HexIt(char* filename)
:   m_bRunning(false)
,	m_bBufferDirty(false)
,	m_uHeight(40)
,	m_uWidth(80)
,	m_uFilePos(0)
,	m_uFileSize(0)
,	m_bShowColor(false)
,	m_bPrintUpper(false)
,	m_bShowByteCount(true)
,	m_bShowASCII(true)
,	m_clipboardByte(0)
,	m_hasClipboard(false)
,	m_selAnchor(-1)
{
	snprintf(m_appVersion, sizeof(m_appVersion), "HexIt v%d.%d", VERSION_MAJOR, VERSION_MINOR);

	m_pFile = new fstream();
	if(m_pFile)
    {
        m_pFile->open(filename);

        // detect file size!
        if( m_pFile->is_open() )
        {
        	m_inputFilename = filename;
            m_pFile->seekg(0, ios::end);
            m_uFileSize = (uint)m_pFile->tellg();
            m_pFile->seekg(0, ios::beg);
        }
    }
}

HexIt::~HexIt()
{
	if( m_pFile )
	{
		m_pFile->close();
		delete m_pFile;
		m_pFile = NULL;
	}
}

void HexIt::print(ostream& output)
{
	if( m_pFile && m_pFile->is_open() )
	{
		m_pFile->seekg(0, ios::beg);
        
		char c[READ_BUFFER_BYTES+1] = {0}; // grab 16 bytes at a time, add 1 to store terminating null
		uint i;
		uint size = 0;
		uint bytes_read = READ_BUFFER_BYTES;
        
		// We need to check if we read all the bytes because we're manually
		// adding the file's stop byte to the sequence. If the number of bytes
		// is aligned to 16 byte width, it wouldn't print the stop byte
		for(i = 0; !m_pFile->eof() && bytes_read == READ_BUFFER_BYTES; i++)
		{
			memset(c,0x00,READ_BUFFER_BYTES);	// reset the buffer in case we don't get 16 bytes
			
			if( !m_pFile->eof() ) // keep going!
			{
				size  = (uint)m_pFile->tellg();
				m_pFile->read(c,READ_BUFFER_BYTES);
                
				bytes_read = (uint)m_pFile->gcount();		// see how much we actually got
				size += bytes_read;
                
				if( bytes_read < READ_BUFFER_BYTES )
				{
					c[bytes_read] = 0x0a;
					bytes_read += 1; // fake stop byte
					
				}
			}
			else // This means we read 16 bytes and hit the eof last round
			{
				bytes_read = 1; // fake stop byte
				c[0] = 0x0a;
			}
            
			renderLine(output, i<<4, c, bytes_read);
			
		}
		output << endl << "bytes: [ 0x" << getCaseFunction() << hex << size << " : " << dec << size << " ]" << endl;
	}
}

void HexIt::editMode()
{
	editInit();
    
	setTerminalSize();
    
	// we're editing so set running flag!
	m_bRunning = true;
    
	if( m_pFile && m_pFile->is_open() )
	{
		// initialize edit state
		m_bBufferDirty = false;
        
		m_pFile->seekg(0, ios::beg);
		m_uFilePos = 0;
        
	    m_buffer << m_pFile->rdbuf();
	    m_buffer.seekg(0, ios::beg);
		// load the screen up with data!
		while(m_bRunning)
		{
         	// output the screen's text   
			renderScreen();
            
 			// accept input
 			TermKeyResult ret;
 			TermKeyKey key;
			
 			ret = termkey_waitkey(m_tk, &key);
            if( key.type == TERMKEY_TYPE_KEYSYM) // a symbol key
            {
             	switch(key.code.sym)
             	{
 				case TERMKEY_SYM_DOWN:
				case TERMKEY_SYM_UP:
				case TERMKEY_SYM_LEFT:
				case TERMKEY_SYM_RIGHT: {
					bool shifted = (key.modifiers & TERMKEY_KEYMOD_SHIFT) != 0;
					if (shifted && m_selAnchor < 0) {
						m_selAnchor = (int64_t)m_cursor.word;
					} else if (!shifted) {
						m_selAnchor = -1;
					}
					if (m_cursor.editing) {
						if (key.code.sym == TERMKEY_SYM_LEFT)  moveNibble(-1);
						if (key.code.sym == TERMKEY_SYM_RIGHT) moveNibble(1);
					} else {
						if (key.code.sym == TERMKEY_SYM_DOWN)  moveCursor(0,  ROW_SIZE);
						if (key.code.sym == TERMKEY_SYM_UP)    moveCursor(0, -ROW_SIZE);
						if (key.code.sym == TERMKEY_SYM_LEFT)  moveCursor(-WORD_SIZE, 0);
						if (key.code.sym == TERMKEY_SYM_RIGHT) moveCursor( WORD_SIZE, 0);
					}
					break;
				}
                 case TERMKEY_SYM_ENTER:
                     toggleEdit(true);
                     break;
                 case TERMKEY_SYM_ESCAPE:
                 	if(m_cursor.editing)
                    	toggleEdit(false);
                    break;
                 default:
                    break;
 				}
            }
            else if( key.type == TERMKEY_TYPE_UNICODE ) // a letter was pressed
            {
                if(key.modifiers & TERMKEY_KEYMOD_CTRL) // COMMAND KEYS
                {
                    switch(key.code.codepoint)
                    {
                    	// Commands
                    	case 'a':
	                    case 'A':
	                    	if (!m_cursor.editing) cmdAnalyzeAI();
	                    	break;

                    	case 'b':
	                    case 'B':
	                    	cmdPageDn();
	                    	break;

                    	case 'c':
	                    case 'C':
	                    	if(!m_cursor.editing)
	                    	{
	                    		cmdCopyByte();
	                    	}
	                    	break;

                    	case 'f':
                    		cmdFillWord();
                    		break;
	                    case 'F': {
	                    	uint val = 0;
	                    	if (promptHex("Fill+Insert", 4, val)) {
	                    		auto* pbuf = m_buffer.rdbuf();
	                    		if (pbuf->pubseekpos(m_cursor.word) == (std::streampos)m_cursor.word) {
	                    			pbuf->sputc((char)((val >> 8) & 0xFF));
	                    			pbuf->sputc((char)(val & 0xFF));
	                    		}
	                    		uint8_t bytes[2] = { (uint8_t)((val >> 8) & 0xFF), (uint8_t)(val & 0xFF) };
	                    		insertBytesAt(m_cursor.word + WORD_SIZE, bytes, 2);
	                    		m_cursor.word += WORD_SIZE;
	                    		checkCursorOffscreen();
	                    		m_bBufferDirty = true;
	                    		char msg[40];
	                    		snprintf(msg, sizeof(msg), "fill+insert %04X", val & 0xFFFF);
	                    		statusMessage(msg);
	                    	}
	                    	break;
	                    }

	                    case 'i':
	                    	cmdInsertWord();
	                    	break;
	                    case 'I':
	                    	cmdInsertWordAt();
	                    	break;

	                    case 'o':
	                    case 'O':
	                    	cmdOutputFile();
	                    	break;
						
						case 'w':
                    	case 'W':
                    		cmdFindByte();
                    		break;

						case 'x':
						case 'X':
	                        m_bRunning = false;
	                        cmdCloseFile();
	                        break;

	                    case 'y':
	                    case 'Y':
	                    	cmdPageUp();
	                    	break;
	                    
	                    case 'v':
	                    case 'V':
	                    	if(!m_cursor.editing) cmdPasteByte();
	                    	break;
	                    default:
	                    	break;
                    }
                }
                else
                {
					switch(key.code.codepoint)
					{
	                    // Editing Keys
						case '0':
							editKey(INPUT_KEY_0);
							break;
						case '1':
							editKey(INPUT_KEY_1);
							break;
						case '2':
							editKey(INPUT_KEY_2);
							break;
						case '3':
							editKey(INPUT_KEY_3);
							break;
						case '4':
							editKey(INPUT_KEY_4);
							break;
						case '5':
							editKey(INPUT_KEY_5);
							break;
						case '6':
							editKey(INPUT_KEY_6);
							break;
						case '7':
							editKey(INPUT_KEY_7);
							break;
						case '8':
							editKey(INPUT_KEY_8);
							break;
						case '9':
							editKey(INPUT_KEY_9);
							break;
						case 'a':
						case 'A':
							editKey(INPUT_KEY_A);
							break;
						case 'b':
						case 'B':
							editKey(INPUT_KEY_B);
							break;
						case 'c':
						case 'C':
							editKey(INPUT_KEY_C);
							break;
						case 'd':
						case 'D':
							editKey(INPUT_KEY_D);
							break;
						case 'e':
						case 'E':
							editKey(INPUT_KEY_E);
							break;
						case 'f':
						case 'F':
							editKey(INPUT_KEY_F);
							break;
						default:
	                    	break;
					}
                }
            }
		}
 	}
    
	editCleanup();
}

void HexIt::setSwitches(uint switches)
{
	m_bShowColor     = switches & SWITCH_COLOR;
    m_bPrintUpper 	 = switches & SWITCH_UPPER;
    m_bShowByteCount = switches & SWITCH_SHOW_BYTE_COUNT;
    m_bShowASCII 	 = switches & SWITCH_SHOW_ASCII;
}

void HexIt::setOutputFilename(const std::string& path)
{
	m_outputFilename = path;
}

case_fptr HexIt::getCaseFunction()
{
	return (m_bPrintUpper ? uppercase : nouppercase );
}

void HexIt::textColor(uint byte_pos, char byte_data)
{
	stringstream text;
	text << hex;

	if( !m_bShowColor )
	{
		text << getCaseFunction() << setw(2) << setfill('0') << (byte_data & 0xFF);
		// ncurses
		waddstr(m_wEditArea, text.str().c_str());
		return;
	}

	// determine if we're at the cursor byte
	if( (byte_pos & 0xFFFFFFFE) == m_cursor.word )
	{
		if( m_cursor.editing )
		{
			text << getCaseFunction() << setw(2) << setfill('0') << (byte_data & 0xFF);
			// ncurses
			wattron(m_wEditArea, COLOR_PAIR(COLOR_EDIT));
			waddstr(m_wEditArea, text.str().c_str());					
			wattroff(m_wEditArea, COLOR_PAIR(COLOR_EDIT));
		}
		else
		{
			text << getCaseFunction() << setw(2) << setfill('0') << (byte_data & 0xFF);
			// ncurses
			wattron(m_wEditArea, COLOR_PAIR(COLOR_HIGHLIGHT));
			waddstr(m_wEditArea, text.str().c_str());
			wattroff(m_wEditArea, COLOR_PAIR(COLOR_HIGHLIGHT));
		}

	}
	else
	{
		text << getCaseFunction() << setw(2) << setfill('0') << (byte_data & 0xFF);
		// ncurses
		wattron(m_wEditArea, COLOR_PAIR(COLOR_EDITOR));
		waddstr(m_wEditArea, text.str().c_str());
		wattroff(m_wEditArea, COLOR_PAIR(COLOR_EDITOR));
	}
}

void HexIt::setTerminalSize()
{
	int rows, columns;
	getmaxyx(stdscr, rows, columns);

	m_uWidth = columns;
	m_uHeight = ROWS_EDIT(rows);

	// we have 4 window areas, they all span the entire window's columns
	// Title Area
	// Editing area
	// Status Area
	// Command Area
	SAFE_DELETE_WINDOW(m_wTitleArea);
	SAFE_DELETE_WINDOW(m_wEditArea);
	SAFE_DELETE_WINDOW(m_wStatusArea);
	SAFE_DELETE_WINDOW(m_wCommandArea);

	m_wTitleArea 	= newwin(ROWS_TITLE,		columns, 0,			0);
	m_wEditArea 	= newwin(ROWS_EDIT(rows),	columns, 1,			0);
	m_wStatusArea	= newwin(ROWS_STATUS,		columns, rows - 3,	0);
	m_wCommandArea	= newwin(ROWS_COMMAND,		columns, rows - 2,	0);
}

void HexIt::renderLine(ostream& output, uint start_byte, char* byte_seq, uint bytes_read)
{
	// put hex byte count to start the row
	// todo, predictively set the width of the row header
	output << hex << getCaseFunction() << setw(7) << setfill('0') << start_byte << ": ";
    
	// output each byte
	for(uint j = 0; j < (uint)READ_BUFFER_BYTES; j++)
	{
		if(!(j&1)) // every even byte index put a seperator
		{
			output << " ";
		}
        
		// each byte will print 2 (padded) hex chars
		if(j < bytes_read)
		{
			char toWrite = byte_seq[j];
            
			// test if we're editing this word, if so use the edit word instead
			// remove the last bit from j so that both bytes we're editing are replaced
			if( m_cursor.editing &&
               m_cursor.word == ( start_byte + (j & (0xFFFE) ) )
               )
			{
				// pull out individual byte
				toWrite = ((m_cursor.editWord >> ((1-(j&1))*8)) & 0xFF);
			}
			output << hex << getCaseFunction() << setw(2) << setfill('0') << (toWrite & 0xFF);
		}
		else
		{
			output << "  ";			// blank if not read
			byte_seq[j] = ' ';		// blank if not read
		}
        
		// adjust ascii for control chars or anything out of bounds
		if(byte_seq[j] < ASCII_MIN || byte_seq[j] > ASCII_MAX)
		{
			byte_seq[j] = '.';
		}
	}
	
	output << "  ; " << byte_seq << " ;"; // output ascii straight onto the end!
	output << endl;
}

void HexIt::renderScreen()
{
	wbkgd(m_wTitleArea,		COLOR_PAIR(COLOR_TITLE));
	wbkgd(m_wEditArea,		COLOR_PAIR(COLOR_EDITOR));
	wbkgd(m_wStatusArea,	COLOR_PAIR(COLOR_EDITOR));
	wbkgd(m_wCommandArea,	COLOR_PAIR(COLOR_COMMAND));

	// Render Title Area
	wmove(m_wTitleArea, 0, 2);
	// App Name
	waddch(m_wTitleArea,' ');
	waddstr(m_wTitleArea, m_appVersion);
	// File Name
	uint centered = HALF_WIDTH(m_uWidth) - HALF_WIDTH((uint)(m_inputFilename.size() + 6)); // + 6 for "File: "
	wmove(m_wTitleArea, 0, centered);
	waddstr(m_wTitleArea, "File: ");
	waddstr(m_wTitleArea, m_inputFilename.c_str());
	// Modified?
	if(m_bBufferDirty)
	{
		wmove(m_wTitleArea, 0, m_uWidth - 11);
		waddstr(m_wTitleArea, "Modified");
	}

	
	// Render Edit Area
	char c[READ_BUFFER_BYTES+1] = {0}; // grab 16 bytes at a time, add 1 to store terminating null
	uint bytes_read = READ_BUFFER_BYTES;

	//for( uint byte = m_uFilePos; byte <= m_uFileSize; byte += 16)
	for( uint line = 0; line < m_uHeight; line++)
	{
		wmove(m_wEditArea, line, 0);

		uint byte = m_uFilePos + line * 16;
        
		if( byte < m_uFileSize )
		{
			m_buffer.seekg(byte, ios::beg);
			memset(c,0x00,READ_BUFFER_BYTES);	// reset the buffer in case we don't get 16 bytes
			
			if( !m_buffer.eof() ) // keep going!
			{
				m_buffer.read(c,READ_BUFFER_BYTES);
				bytes_read = (uint)m_buffer.gcount();		// see how much we actually got
                
				if( bytes_read < READ_BUFFER_BYTES )
				{
					c[bytes_read] = 0x0a;
					bytes_read += 1; // fake stop byte
				}
			}
			else // This means we read 16 bytes and hit the eof last round
			{
				bytes_read = 1; // fake stop byte
				c[0] = 0x0a;
			}
			
			//renderLine(output, byte, &c[0], bytes_read);
			stringstream output;
			

			if(m_bShowByteCount) 
			{
				wattron(m_wEditArea, COLOR_PAIR(COLOR_EDITOR));
				output << hex << getCaseFunction() << setw(7) << setfill('0') << byte << ": ";
				// ncurses
    			waddstr(m_wEditArea, output.str().c_str());
    			output.str("");
    			wattroff(m_wEditArea, COLOR_PAIR(COLOR_EDITOR));
    		}

			// output each byte
		
			for(uint j = 0; j < (uint)READ_BUFFER_BYTES; j++)
			{
				if(!(j&1)) // every even byte index put a seperator
				{
                    if(!(!m_bShowByteCount && j==0))
                    {
                        // ncurses
                        wattron(m_wEditArea, COLOR_PAIR(COLOR_EDITOR));
                        waddch(m_wEditArea, ' ');
                        wattroff(m_wEditArea, COLOR_PAIR(COLOR_EDITOR));
                    }
				}
		        
				// each byte will print 2 (padded) hex chars
				if(j < bytes_read)
				{
					char toWrite = c[j];

					// test if we're editing this word, if so use the edit word instead
					// remove the last bit from j so that both bytes we're editing are replaced
					if( m_cursor.editing &&
		                m_cursor.word == (byte + (j & (0xFFFE))) )
					{
						// pull out individual byte
						toWrite = ((m_cursor.editWord >> ((1-(j&1))*8)) & 0xFF);
					}
					bool isCursorByte = (((byte + j) & 0xFFFFFFFEu) == m_cursor.word);
					if (inSelection(byte + j) && !isCursorByte) {
						std::stringstream tmp;
						tmp << hex << getCaseFunction() << setw(2) << setfill('0') << (toWrite & 0xFF);
						wattron(m_wEditArea, COLOR_PAIR(COLOR_SELECTION));
						waddstr(m_wEditArea, tmp.str().c_str());
						wattroff(m_wEditArea, COLOR_PAIR(COLOR_SELECTION));
					} else {
						textColor(byte+j, toWrite);
					}
				}
				else
				{
					// ncurses
					waddch(m_wEditArea, ' '); waddch(m_wEditArea, ' ');
					c[j] = ' ';		// blank if not read
				}
		        
				// adjust ascii for control chars or anything out of bounds
				if(c[j] < ASCII_MIN || c[j] > ASCII_MAX)
				{
					c[j] = '.';
				}
			}
			
			if( m_bShowASCII )
			{
				wattron(m_wEditArea, COLOR_PAIR(COLOR_EDITOR));
				output << "  ; " << c << " ;"; // output ascii straight onto the end!
				output << endl;

				// ncurses
				waddstr(m_wEditArea, output.str().c_str());
				output.str("");
				wattroff(m_wEditArea, COLOR_PAIR(COLOR_EDITOR));
			}	
		}
		else
		{
			// ncurses
			waddch(m_wEditArea, '\n');
		}
	}
	setCursorPos();
	
	// Render Status Area
	{
		char left[128];
		uint pct = (m_uFileSize > 0) ? (uint)((100.0 * m_cursor.word) / m_uFileSize) : 0;
		snprintf(left, sizeof(left), " %07X / %07X (%3u%%)  %s%s",
		         m_cursor.word, m_uFileSize, pct,
		         m_cursor.editing ? "EDIT" : "READ",
		         m_bBufferDirty ? "  *modified*" : "");
		wmove(m_wStatusArea, 0, 0);
		waddstr(m_wStatusArea, left);

		if (!m_statusMessage.empty()) {
			int msglen = (int)m_statusMessage.size();
			int col = (int)m_uWidth - msglen - 1;
			if (col > (int)strlen(left) + 2) {
				wmove(m_wStatusArea, 0, col);
				waddstr(m_wStatusArea, m_statusMessage.c_str());
			}
			m_statusMessage.clear(); // one-frame
		}
	}

	// Render Command Area
	wmove(m_wCommandArea, 0, HALF_WIDTH(m_uWidth)-HALF_WIDTH(5));
	waddstr(m_wCommandArea, "HexIt");
	wmove(m_wCommandArea, 1, HALF_WIDTH(m_uWidth)-HALF_WIDTH(7));
	waddstr(m_wCommandArea, "COMMAND");

	// update to screen
	wrefresh(m_wTitleArea);
	wrefresh(m_wStatusArea);
	wrefresh(m_wCommandArea);
    wrefresh(m_wEditArea); // refresh edit last so the cursor goes there
}

void HexIt::setCursorPos(uint _word, uint _nibble)
{
	m_cursor.word = _word;
	m_cursor.nibble = _nibble;
}

// no parameters means set it with ncurses to the window
void HexIt::setCursorPos()
{
	// calculate the row and column by comparing m_uFilePos and m_cursor.word
	assert(m_cursor.word >= m_uFilePos);
    
	uint nibble = (m_cursor.editing ? m_cursor.nibble : 0);
	uint row = getCursorRow();
	uint col = COLUMN_POS(getCursorColumn()) + nibble;
    
	wmove(m_wEditArea, row, col);
	//move(0,0);
}

uint HexIt::getCursorRow()
{
	uint offset = m_cursor.word - m_uFilePos;
	uint row = offset >> 4; // how many rows of bytes before the cursor.
	
	return row;
}

uint HexIt::getCursorColumn()
{
    
	uint offset = m_cursor.word - m_uFilePos;
	uint column = offset & 0xF;
	
	return column;
}

void HexIt::checkCursorOffscreen()
{
	while(m_cursor.word >= m_uFilePos + (m_uHeight<<4) || m_cursor.word > m_uFileSize)
	{
		m_uFilePos = min( m_uFilePos + 0x10, maxFilePos());
	}
	while(m_cursor.word < m_uFilePos)
	{
		m_uFilePos = (m_uFilePos >= 0x10 ? m_uFilePos - 0x10 : 0);
	}
}

void HexIt::moveNibble(int x)
{
	m_cursor.nibble = (m_cursor.nibble + x) & 0x3;
}

uint HexIt::maxFilePos()
{
	// take the file size and align it to 16 bytes to get
	// the start of the the last byte's 16 byte sequence
	uint start_byte = m_uFileSize & ~(0xF);

	// now subtract the number of 16 byte rows in the buffer from a
	// full screen's buffer or end of the file's start_byte, whichever is larger
	start_byte = max(start_byte, m_uHeight<<4) - (m_uHeight << 4);

	return start_byte;
}

void HexIt::moveCursor(int x, int y)
{
	int newX = m_cursor.word + x;
	int newY = m_cursor.word + y;
	// don't allow someone to scroll past the end until a word is inserted
	if( (x < 0 && newX >= 0) ||
	    (x > 0 && newX < m_uFileSize) )
	{
		m_cursor.word = newX;
	}
	
    
	if( (y < 0 && newY >= 0) ||
	    (y > 0 && newY < (int)(m_uFileSize & 0xFFFFFFF0u)) )
    {
		m_cursor.word = newY;
	}

    // first adjust file pos
	checkCursorOffscreen();
    
    // then adjust cursor on screen or not
	setCursorPos();
}

void HexIt::toggleEdit(bool save)
{
	if(!m_cursor.editing)
	{
		// initiating an edit, save the current word;
		m_cursor.backup = 0;
        
		// reset to the first nibble
		m_cursor.nibble = 0;
        
		// because bytes are read one at a time into "chars"
		// don't read the bytes directly into m_cursor.backup
		char read_buf[WORD_SIZE];
        
		m_buffer.seekg(m_cursor.word, ios::beg);
		m_buffer.read(read_buf,WORD_SIZE); // read 4 nibbles, or 2 bytes
        
		// store the two bytes back into a word inside the lower 16 bytes in m_cursor.backup
		// throw away the 16 most sig bits.
		m_cursor.backup = (((read_buf[0] & 0xFF) << 8) | (read_buf[1] & 0xFF)) & 0xFFFF;
		m_buffer.seekg(m_uFilePos);
        
		// initialize the edit word to be the same as what's in the edit buffer
		m_cursor.editWord = m_cursor.backup;
        
	}
	else // we are ending an edit
	{
		// don't bother if we didn't change anything
		if( m_cursor.backup != m_cursor.editWord )
		{
            std::stringbuf *pbuf = m_buffer.rdbuf();
            if( pbuf->pubseekpos(m_cursor.word) == m_cursor.word )
            {
                // if we pressed escape or something reset the word
                uint16_t restore = ( save ? m_cursor.editWord : m_cursor.backup ) & 0xFFFF;
                // because we're putting chars and not 16 bytes at once we need to reverse
                // the order we write them to maintain endianness
                pbuf->sputc(((char*)&restore)[1]);
                pbuf->sputc(((char*)&restore)[0]);
            }
            
            // if we're not saving we restored the stream, so it's not an edit
            // but don't clear the dirty flag when not editing, only on save!
            if(save)
                m_bBufferDirty = true;
		}
	}
	m_cursor.editing = !m_cursor.editing;
}

void HexIt::editKey(uint nibble)
{
	// are we already editing?
	if( !m_cursor.editing )
	{
		toggleEdit(false);
	}
    
	// to put the new nibble in place, first clear all bits
	uint mask = NIBBLE_MASK(m_cursor.nibble);
	m_cursor.editWord &= ~mask;
    
	// then |= with the nibble itself.
	uint shift = NIBBLE_SHIFT(m_cursor.nibble);
	m_cursor.editWord |= (nibble << shift);
    
	// advance the nibble pointer
	m_cursor.nibble++;
	m_cursor.nibble &= 0x3; // % 4
    
	// print the last input nibble to the dirty screen
	// addch(HEX_NIBBLE[nibble]);
}

void HexIt::cmdPageDn()
{
	uint page = m_uHeight * ROW_SIZE;
	uint maxWord = (m_uFileSize > 0) ? ((m_uFileSize - 1) & ~0x1u) : 0;
	uint target = m_cursor.word + page;
	if (target > maxWord) target = maxWord;
	m_cursor.word = target;
	if (m_cursor.editing) toggleEdit(false); // abort in-flight edit
	checkCursorOffscreen();
}

void HexIt::cmdCopyByte()
{
	char b = 0;
	m_buffer.seekg(m_cursor.word, std::ios::beg);
	m_buffer.read(&b, 1);
	m_clipboardByte = (uint8_t)b;
	m_hasClipboard = true;

	char msg[40];
	snprintf(msg, sizeof(msg), "copied %02X at %07X", m_clipboardByte, m_cursor.word);
	statusMessage(msg);
	m_buffer.seekg(m_uFilePos);
}

void HexIt::cmdFindByte()
{
	uint val = 0;
	if (!promptHex("Find byte", 2, val)) return;
	uint8_t needle = (uint8_t)(val & 0xFF);

	std::string data = m_buffer.str();
	size_t start = (size_t)m_cursor.word + 1;
	size_t hit = std::string::npos;
	for (size_t i = start; i < data.size(); ++i) {
		if ((uint8_t)data[i] == needle) { hit = i; break; }
	}
	if (hit == std::string::npos) {
		// wrap from beginning to just before start
		for (size_t i = 0; i < start && i < data.size(); ++i) {
			if ((uint8_t)data[i] == needle) { hit = i; break; }
		}
	}
	if (hit == std::string::npos) {
		char msg[32];
		snprintf(msg, sizeof(msg), "byte %02X not found", needle);
		statusMessage(msg);
		return;
	}

	m_cursor.word = (uint)(hit & ~0x1u);
	checkCursorOffscreen();

	char msg[40];
	snprintf(msg, sizeof(msg), "found %02X @ %07zX", needle, hit);
	statusMessage(msg);
}

void HexIt::cmdFillWord()
{
	uint val = 0;
	if (!promptHex("Fill word", 4, val)) return;

	auto* pbuf = m_buffer.rdbuf();
	if (pbuf->pubseekpos(m_cursor.word) == (std::streampos)m_cursor.word) {
		pbuf->sputc((char)((val >> 8) & 0xFF));
		pbuf->sputc((char)(val & 0xFF));
		m_bBufferDirty = true;
		char msg[40];
		snprintf(msg, sizeof(msg), "filled %04X at %07X", val & 0xFFFF, m_cursor.word);
		statusMessage(msg);
	}
}

void HexIt::cmdInsertWord()
{
	uint8_t zeros[2] = { 0x00, 0x00 };
	insertBytesAt(m_cursor.word, zeros, 2);
	checkCursorOffscreen();
	statusMessage("inserted 0000");
}

void HexIt::cmdInsertWordAt()
{
	uint val = 0;
	if (!promptHex("Insert word", 4, val)) return;
	uint8_t bytes[2] = { (uint8_t)((val >> 8) & 0xFF), (uint8_t)(val & 0xFF) };
	insertBytesAt(m_cursor.word, bytes, 2);
	checkCursorOffscreen();
	char msg[40];
	snprintf(msg, sizeof(msg), "inserted %04X", val & 0xFFFF);
	statusMessage(msg);
}

void HexIt::insertBytesAt(uint pos, const uint8_t* bytes, uint n)
{
	if (n == 0) return;
	std::string data = m_buffer.str();
	if (pos > data.size()) pos = (uint)data.size();
	data.insert(pos, reinterpret_cast<const char*>(bytes), n);

	// rebuild the stringstream from the new data
	m_buffer.str(std::string());
	m_buffer.clear();
	m_buffer.write(data.data(), (std::streamsize)data.size());
	m_buffer.seekg(0, std::ios::beg);

	m_uFileSize = (uint)data.size();
	m_bBufferDirty = true;
}

void HexIt::cmdOutputFile()
{
	saveToDisk();
}

void HexIt::cmdCloseFile()
{
	if (m_bBufferDirty) {
		saveToDisk();
	}
	// m_bRunning is already set to false by the keybinding site.
}

void HexIt::cmdPageUp()
{
	uint page = m_uHeight * ROW_SIZE;
	m_cursor.word = (m_cursor.word > page) ? (m_cursor.word - page) : 0;
	if (m_cursor.editing) toggleEdit(false);
	checkCursorOffscreen();
}

void HexIt::cmdPasteByte()
{
	if (!m_hasClipboard) {
		statusMessage("clipboard empty");
		return;
	}
	auto* pbuf = m_buffer.rdbuf();
	if (pbuf->pubseekpos(m_cursor.word) == (std::streampos)m_cursor.word) {
		pbuf->sputc((char)m_clipboardByte);
		m_bBufferDirty = true;
		statusMessage("pasted");
	}
}


void HexIt::editInit()
{
	// let's init termkey now
	m_tk = termkey_new(0, 0);

	initscr();
	raw();
	noecho();
	keypad(stdscr, TRUE);

	// debug getch to wait for debugger!
	//getch();

	// if we don't have color ability then it's off regardless
	if(!has_colors())
		m_bShowColor = false;

	// this should be defaulted to on now
	if(m_bShowColor)
	{
		start_color();

		/*
			ncurses colors
		        COLOR_BLACK   0
		        COLOR_RED     1
		        COLOR_GREEN   2
		        COLOR_YELLOW  3
		        COLOR_BLUE    4
		        COLOR_MAGENTA 5
		        COLOR_CYAN    6
		        COLOR_WHITE   7
		*/
        // init your color pairs! FG, BG
		init_pair(COLOR_STANDARD,		COLOR_WHITE,		COLOR_BLACK);
		init_pair(COLOR_HIGHLIGHT,		COLOR_BLACK,		COLOR_WHITE);
		init_pair(COLOR_EDIT,			COLOR_RED,			COLOR_WHITE);

		init_pair(COLOR_TITLE,			COLOR_BLUE,			COLOR_WHITE);
		init_pair(COLOR_EDITOR,			COLOR_WHITE,		COLOR_BLUE);
		init_pair(COLOR_COMMAND,		COLOR_BLUE,		COLOR_WHITE);
		init_pair(COLOR_SELECTION,		COLOR_BLACK,		COLOR_YELLOW);
	}
}

void HexIt::editCleanup()
{
    wclear(m_wTitleArea);
    wclear(m_wEditArea);
    wclear(m_wStatusArea);
    wclear(m_wCommandArea);
    
	SAFE_DELETE_WINDOW(m_wTitleArea);
	SAFE_DELETE_WINDOW(m_wEditArea);
	SAFE_DELETE_WINDOW(m_wStatusArea)
	SAFE_DELETE_WINDOW(m_wCommandArea);
    
    clear();
    refresh();
	endwin();

	// delete termkey object!
	if( m_tk )
	{
		termkey_destroy(m_tk);
		m_tk = NULL;
	}

}

void HexIt::statusMessage(const std::string& msg)
{
	m_statusMessage = msg;
}

bool HexIt::inSelection(uint byte_pos) const
{
	if (m_selAnchor < 0) return false;
	uint a = (uint)m_selAnchor;
	uint b = m_cursor.word + 1; // include the cursor's second byte
	uint lo = (a < b) ? a : b;
	uint hi = (a > b) ? a : b;
	return byte_pos >= lo && byte_pos <= hi;
}

bool HexIt::promptHex(const char* label, uint nibbles, uint& out)
{
	if (nibbles == 0 || nibbles > 8) return false;

	std::string entered;
	out = 0;

	auto redraw = [&]() {
		wbkgd(m_wCommandArea, COLOR_PAIR(COLOR_COMMAND));
		werase(m_wCommandArea);
		wmove(m_wCommandArea, 0, 1);
		waddstr(m_wCommandArea, label);
		waddstr(m_wCommandArea, ": ");
		waddstr(m_wCommandArea, entered.c_str());
		for (size_t i = entered.size(); i < nibbles; ++i) waddch(m_wCommandArea, '_');
		wmove(m_wCommandArea, 1, 1);
		waddstr(m_wCommandArea, "[0-9A-F]  Enter=accept  Esc=cancel  Backspace=delete");
		wrefresh(m_wCommandArea);
	};

	redraw();

	while (true) {
		TermKeyKey key;
		if (termkey_waitkey(m_tk, &key) != TERMKEY_RES_KEY) continue;

		if (key.type == TERMKEY_TYPE_KEYSYM) {
			if (key.code.sym == TERMKEY_SYM_ENTER) {
				while (entered.size() < nibbles) entered.push_back('0');
				out = 0;
				for (size_t i = 0; i < entered.size(); ++i) {
					char c = entered[i];
					uint v = 0;
					if (c >= '0' && c <= '9') v = c - '0';
					else if (c >= 'a' && c <= 'f') v = 10 + (c - 'a');
					else if (c >= 'A' && c <= 'F') v = 10 + (c - 'A');
					out = (out << 4) | v;
				}
				return true;
			}
			if (key.code.sym == TERMKEY_SYM_ESCAPE) return false;
			if (key.code.sym == TERMKEY_SYM_BACKSPACE) {
				if (!entered.empty()) entered.pop_back();
				redraw();
				continue;
			}
		} else if (key.type == TERMKEY_TYPE_UNICODE) {
			char c = (char)key.code.codepoint;
			bool isHex = (c >= '0' && c <= '9') ||
			             (c >= 'a' && c <= 'f') ||
			             (c >= 'A' && c <= 'F');
			if (isHex && entered.size() < nibbles) {
				entered.push_back(c);
				redraw();
			}
		}
	}
}

bool HexIt::saveToDisk()
{
	std::string target = m_outputFilename.empty() ? m_inputFilename : m_outputFilename;
	if (target.empty()) {
		statusMessage("no output filename");
		return false;
	}

	std::string data = m_buffer.str();
	std::ofstream out(target.c_str(), std::ios::binary | std::ios::trunc);
	if (!out.is_open()) {
		statusMessage("save failed: cannot open " + target);
		return false;
	}
	out.write(data.data(), (std::streamsize)data.size());
	if (!out.good()) {
		statusMessage("save failed: write error");
		return false;
	}
	out.close();
	m_bBufferDirty = false;
	statusMessage("saved " + std::to_string(data.size()) + " bytes to " + target);
	return true;
}

std::string HexIt::jsonEscape(const std::string& s)
{
	std::string out;
	out.reserve(s.size() + 8);
	for (size_t i = 0; i < s.size(); ++i) {
		char c = s[i];
		switch (c) {
			case '"':  out += "\\\""; break;
			case '\\': out += "\\\\"; break;
			case '\n': out += "\\n";  break;
			case '\r': out += "\\r";  break;
			case '\t': out += "\\t";  break;
			default:
				if ((unsigned char)c < 0x20) {
					char buf[8];
					snprintf(buf, sizeof(buf), "\\u%04x", (unsigned char)c);
					out += buf;
				} else {
					out += c;
				}
		}
	}
	return out;
}

std::string HexIt::extractContent(const std::string& json)
{
	const std::string needle = "\"content\":";
	size_t p = json.find(needle);
	if (p == std::string::npos) return std::string();
	p += needle.size();
	while (p < json.size() && (json[p] == ' ' || json[p] == '\t' || json[p] == '\n')) ++p;
	if (p >= json.size() || json[p] != '"') return std::string();
	++p;

	std::string out;
	while (p < json.size()) {
		char c = json[p++];
		if (c == '"') return out;
		if (c == '\\' && p < json.size()) {
			char esc = json[p++];
			switch (esc) {
				case '"':  out += '"';  break;
				case '\\': out += '\\'; break;
				case '/':  out += '/';  break;
				case 'n':  out += '\n'; break;
				case 'r':  out += '\r'; break;
				case 't':  out += '\t'; break;
				case 'b':  out += '\b'; break;
				case 'f':  out += '\f'; break;
				case 'u':
					if (p + 4 <= json.size()) {
						unsigned int cp = 0;
						for (int i = 0; i < 4; ++i) {
							char hc = json[p + i];
							cp <<= 4;
							if (hc >= '0' && hc <= '9') cp |= hc - '0';
							else if (hc >= 'a' && hc <= 'f') cp |= 10 + (hc - 'a');
							else if (hc >= 'A' && hc <= 'F') cp |= 10 + (hc - 'A');
						}
						p += 4;
						if (cp < 0x80) {
							out += (char)cp;
						} else if (cp < 0x800) {
							out += (char)(0xC0 | (cp >> 6));
							out += (char)(0x80 | (cp & 0x3F));
						} else {
							out += (char)(0xE0 | (cp >> 12));
							out += (char)(0x80 | ((cp >> 6) & 0x3F));
							out += (char)(0x80 | (cp & 0x3F));
						}
					}
					break;
				default: out += esc; break;
			}
		} else {
			out += c;
		}
	}
	return out;
}

std::string HexIt::selectionAsHex()
{
	if (m_selAnchor < 0) return std::string();
	uint a = (uint)m_selAnchor;
	uint b = m_cursor.word + 1;
	uint lo = (a < b) ? a : b;
	uint hi = (a > b) ? a : b;
	if (m_uFileSize > 0 && hi >= m_uFileSize) hi = m_uFileSize - 1;

	std::string data = m_buffer.str();
	std::string out;
	out.reserve((hi - lo + 1) * 3);
	for (uint i = lo; i <= hi; ++i) {
		char buf[4];
		snprintf(buf, sizeof(buf), "%02X ", (unsigned char)data[i]);
		out += buf;
	}
	if (!out.empty() && out.back() == ' ') out.pop_back();
	return out;
}

std::string HexIt::aiAnalyze(const std::string& bytes_hex)
{
	const char* key = std::getenv(AI_ENV_VAR_NAME);
	if (!key || !*key) {
		return std::string("Error: ") + AI_ENV_VAR_NAME + " is not set in the environment.";
	}

	std::string sys = "You are a reverse-engineering assistant. The user will paste a short hex byte sequence. Describe likely format, structure, and notable values. Keep it under 300 words.";
	std::string body = std::string("{\"model\":\"gpt-4o-mini\",\"messages\":[")
		+ "{\"role\":\"system\",\"content\":\"" + jsonEscape(sys) + "\"},"
		+ "{\"role\":\"user\",\"content\":\""   + jsonEscape(bytes_hex) + "\"}]}";

	char tmpl[] = "/tmp/hexit-aiXXXXXX";
	int fd = mkstemp(tmpl);
	if (fd < 0) return "Error: mkstemp failed";
	ssize_t written = write(fd, body.data(), body.size());
	close(fd);
	if (written != (ssize_t)body.size()) {
		unlink(tmpl);
		return "Error: failed to write temp body";
	}

	// The API key stays in the inherited environment; the shell expands it at exec time.
	std::string cmd = std::string("curl -sS -X POST https://api.openai.com/v1/chat/completions ")
		+ "-H 'Content-Type: application/json' "
		+ "-H \"Authorization: Bearer $" AI_ENV_VAR_NAME "\" "
		+ "--data-binary @" + tmpl + " 2>&1";

	FILE* pipe = popen(cmd.c_str(), "r");
	if (!pipe) { unlink(tmpl); return "Error: popen failed"; }
	std::string resp;
	char chunk[4096];
	size_t n;
	while ((n = fread(chunk, 1, sizeof(chunk), pipe)) > 0) resp.append(chunk, n);
	int rc = pclose(pipe);
	unlink(tmpl);

	if (rc != 0) {
		return "Error: curl exited " + std::to_string(rc) + "\n" + resp;
	}
	std::string content = extractContent(resp);
	if (content.empty()) {
		return std::string("Error: no content in response\n") + resp.substr(0, 800);
	}
	return content;
}

void HexIt::showPopup(const std::string& title, const std::string& body)
{
	int rows, cols;
	getmaxyx(stdscr, rows, cols);
	int h = (rows * 6) / 10; if (h < 8) h = (rows < 8 ? rows - 2 : 8);
	int w = (cols * 7) / 10; if (w < 40) w = (cols < 40 ? cols - 2 : 40);
	int y = (rows - h) / 2;
	int x = (cols - w) / 2;

	WINDOW* pop = newwin(h, w, y, x);
	keypad(pop, TRUE);
	box(pop, 0, 0);

	// word-wrap body to (w-2) columns
	std::vector<std::string> lines;
	{
		std::string cur;
		for (size_t i = 0; i < body.size(); ++i) {
			char c = body[i];
			if (c == '\n' || (int)cur.size() >= w - 2) {
				lines.push_back(cur);
				cur.clear();
				if (c == '\n') continue;
			}
			cur += c;
		}
		if (!cur.empty()) lines.push_back(cur);
	}

	int viewH = h - 2 - 1; // top/bottom border, one footer line
	int offset = 0;

	auto redraw = [&]() {
		werase(pop);
		box(pop, 0, 0);
		if (!title.empty()) {
			mvwprintw(pop, 0, 2, " %s ", title.c_str());
		}
		for (int i = 0; i < viewH && (offset + i) < (int)lines.size(); ++i) {
			mvwaddstr(pop, 1 + i, 1, lines[offset + i].c_str());
		}
		std::string footer = "  Up/Down=scroll  any other key closes  ";
		mvwaddstr(pop, h - 1, 2, footer.c_str());
		wrefresh(pop);
	};

	redraw();

	while (true) {
		TermKeyKey key;
		if (termkey_waitkey(m_tk, &key) != TERMKEY_RES_KEY) continue;
		if (key.type == TERMKEY_TYPE_KEYSYM) {
			if (key.code.sym == TERMKEY_SYM_UP) {
				if (offset > 0) { offset--; redraw(); }
				continue;
			}
			if (key.code.sym == TERMKEY_SYM_DOWN) {
				if (offset + viewH < (int)lines.size()) { offset++; redraw(); }
				continue;
			}
			if (key.code.sym == TERMKEY_SYM_PAGEUP) {
				offset = (offset > viewH) ? offset - viewH : 0; redraw(); continue;
			}
			if (key.code.sym == TERMKEY_SYM_PAGEDOWN) {
				if (offset + viewH < (int)lines.size())
					offset = std::min(offset + viewH, (int)lines.size() - viewH);
				redraw(); continue;
			}
		}
		break;
	}

	delwin(pop);
	touchwin(stdscr);
	refresh();
	renderScreen();
}

void HexIt::cmdAnalyzeAI()
{
	if (m_selAnchor < 0) {
		statusMessage("select bytes first (Shift+arrows)");
		return;
	}
	std::string hex = selectionAsHex();
	statusMessage("calling OpenAI...");
	renderScreen();
	std::string response = aiAnalyze(hex);
	showPopup("Analyze Selection", response);
}
