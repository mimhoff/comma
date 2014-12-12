// This file is part of comma, a generic and flexible library
// Copyright (c) 2011 The University of Sydney
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
// 1. Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
// 3. All advertising materials mentioning features or use of this software
//    must display the following acknowledgement:
//    This product includes software developed by the The University of Sydney.
// 4. Neither the name of the The University of Sydney nor the
//    names of its contributors may be used to endorse or promote products
//    derived from this software without specific prior written permission.
//
// NO EXPRESS OR IMPLIED LICENSES TO ANY PARTY'S PATENT RIGHTS ARE
// GRANTED BY THIS LICENSE.  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT
// HOLDERS AND CONTRIBUTORS \"AS IS\" AND ANY EXPRESS OR IMPLIED
// WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
// BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
// WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
// OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
// IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.


/// @author vsevolod vlaskine

#include <iostream>
#include <boost/bind.hpp>
#include <boost/function.hpp>
#include <boost/property_tree/info_parser.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/xml_parser.hpp>
#include <boost/regex.hpp>
#include <comma/base/exception.h>
#include <comma/application/contact_info.h>
#include <comma/application/command_line_options.h>
#include <comma/application/signal_flag.h>
#include <comma/name_value/ptree.h>
#include <comma/xpath/xpath.h>

static void usage()
{
    std::cerr << std::endl;
    std::cerr << "take a stream of name-value style input on stdin," << std::endl;
    std::cerr << "output value at given path on stdout" << std::endl;
    std::cerr << std::endl;
    std::cerr << "usage: cat data.xml | name-value-convert <from> [<options>]" << std::endl;
    std::cerr << std::endl;
    std::cerr << "data options" << std::endl;
    std::cerr << "    --from <format>: input format; default name-value" << std::endl;
    std::cerr << "    --to <format>: output format; default name-value" << std::endl;
    std::cerr << std::endl;
    std::cerr << "formats" << std::endl;
    std::cerr << "    info: info data (see boost::property_tree)" << std::endl;
    std::cerr << "    ini: ini data" << std::endl;
    std::cerr << "    json: json data" << std::endl;
    std::cerr << "    name-value: name=value-style data; e.g. x={a=1,b=2},y=3" << std::endl;
    std::cerr << "    path-value: path=value-style data; e.g. x/a=1,x/b=2,y=3" << std::endl;
    std::cerr << "    xml: xml data" << std::endl;
    std::cerr << std::endl;
    std::cerr << "name/path-value options:" << std::endl;
    std::cerr << "    --equal-sign,-e=<equal sign>: default '='" << std::endl;
    std::cerr << "    --delimiter,-d=<delimiter>: default ','" << std::endl;
    std::cerr << "    --show-path-indices,--indices: show indices for array items e.g. y[0]/x/z[1]=\"a\"" << std::endl;
    std::cerr << "    --no-brackets: use with --show-path-indices - above, show indices as path elements e.g. y/0/x/z/1=\"a\"" << std::endl;
    std::cerr << std::endl;
    std::cerr << "path-value options:" << std::endl;
    std::cerr << "    --take-last: if paths are repeated, take last path=value" << std::endl;
    std::cerr << "    --verify-unique,--unique-input: ensure that all input paths are unique (takes precedence over --take-last)" << std::endl;
    std::cerr << "warning: if paths are repeated, output value selected from these inputs in not deterministic" << std::endl;
    std::cerr << std::endl;
    std::cerr << "data flow options:" << std::endl;
    std::cerr << "    --linewise,-l: if present, treat each input line as a record" << std::endl;
    std::cerr << "                   if absent, treat all of the input as one record" << std::endl;
    std::cerr << std::endl;
    std::cerr << comma::contact_info << std::endl;
    std::cerr << std::endl;
    exit( 1 );
}

static char equal_sign;
static char delimiter;
static bool linewise;
typedef comma::property_tree::path_mode path_mode;
static path_mode indices_mode = comma::property_tree::disabled;
static comma::property_tree::check_repeated_paths check_type( comma::property_tree::no_check );

enum Types { ini, info, json, xml, name_value, path_value };

template < Types Type > struct traits {};

template <> struct traits< ini >
{
    static void input( std::istream& is, boost::property_tree::ptree& ptree ) { boost::property_tree::read_ini( is, ptree ); }
    static void output( std::ostream& os, boost::property_tree::ptree& ptree, path_mode ) { boost::property_tree::write_ini( os, ptree ); }
};

template <> struct traits< info >
{
    static void input( std::istream& is, boost::property_tree::ptree& ptree ) { boost::property_tree::read_info( is, ptree ); }
    static void output( std::ostream& os, boost::property_tree::ptree& ptree, path_mode ) { boost::property_tree::write_info( os, ptree ); }
};

template <> struct traits< json >
{
    static void input( std::istream& is, boost::property_tree::ptree& ptree ) { boost::property_tree::read_json( is, ptree ); }
    static void output( std::ostream& os, boost::property_tree::ptree& ptree, path_mode ) { boost::property_tree::write_json( os, ptree ); }
};

template <> struct traits< xml >
{
    static void input( std::istream& is, boost::property_tree::ptree& ptree ) { boost::property_tree::read_xml( is, ptree ); }
    static void output( std::ostream& os, boost::property_tree::ptree& ptree, path_mode ) { boost::property_tree::write_xml( os, ptree ); }
};

template <> struct traits< name_value >
{
    // todo: handle indented input (quick and dirty: use exceptions)
    static void input( std::istream& is, boost::property_tree::ptree& ptree ) { comma::property_tree::from_name_value( is, ptree, equal_sign, delimiter ); }
    static void output( std::ostream& os, boost::property_tree::ptree& ptree, path_mode ) { comma::property_tree::to_name_value( os, ptree, !linewise, equal_sign, delimiter ); }
};

template <> struct traits< path_value > // quick and dirty
{
    static void input( std::istream& is, boost::property_tree::ptree& ptree )
    {
        std::string s;
        if( linewise )
        {
            std::getline( is, s );
        }
        else
        {
            while( is.good() && !is.eof() ) // quick and dirty: read to the end of file
            {
                std::string t;
                std::getline( is, t );
                std::string::size_type pos = t.find_first_not_of( ' ' );
                if( pos == std::string::npos || t[pos] == '#' ) { continue; }
                s += t + delimiter;
            }
        }
        ptree = comma::property_tree::from_path_value_string( s, equal_sign, delimiter, check_type );
    }
    static void output( std::ostream& os, boost::property_tree::ptree& ptree, path_mode mode) { comma::property_tree::to_path_value( os, ptree, mode, equal_sign, delimiter ); if( delimiter == '\n' ) { os << std::endl; } }
};

int main( int ac, char** av )
{
    try
    {
        comma::command_line_options options( ac, av );
        if( options.exists( "--help,-h" ) ) { usage(); }
        std::string from = options.value< std::string >( "--from", "name-value" );
        std::string to = options.value< std::string >( "--to", "name-value" );
        equal_sign = options.value( "--equal-sign,-e", '=' );
        linewise = options.exists( "--linewise,-l" );
        if ( options.exists( "--take-last" ) ) check_type = comma::property_tree::take_last;
        if ( options.exists( "--verify-unique,--unique-input" ) ) check_type = comma::property_tree::unique_input;
        char default_delimiter = ( to == "path-value" || from == "path-value" ) && !linewise ? '\n' : ',';
        delimiter = options.value( "--delimiter,-d", default_delimiter );
        void ( * input )( std::istream& is, boost::property_tree::ptree& ptree );
        void ( * output )( std::ostream& is, boost::property_tree::ptree& ptree, path_mode );
        if( from == "ini" ) { input = &traits< ini >::input; }
        else if( from == "info" ) { input = &traits< info >::input; }
        else if( from == "json" ) { input = &traits< json >::input; }
        else if( from == "xml" ) { input = &traits< xml >::input; }
        else if( from == "path-value" ) { input = &traits< path_value >::input; }
        else { input = &traits< name_value >::input; }
        if( to == "ini" ) { output = &traits< ini >::output; }
        else if( to == "info" ) { output = &traits< info >::output; }
        else if( to == "json" ) { output = &traits< json >::output; }
        else if( to == "xml" ) { output = &traits< xml >::output; }
        else if( to == "path-value" ) { output = &traits< path_value >::output; }
        else { output = &traits< name_value >::output; }
        if( options.exists( "--show-path-indices,--indices" ) ) 
        {
            if( options.exists( "--no-brackets" ) ) { indices_mode = comma::property_tree::without_brackets; }
            else { indices_mode = comma::property_tree::with_brackets; }
        }
        if( linewise )
        {
            comma::signal_flag is_shutdown;
            while( std::cout.good() )
            {
                std::string line;
                std::getline( std::cin, line );
                if( is_shutdown || !std::cin.good() || std::cin.eof() ) { break; }
                std::istringstream iss( line );
                boost::property_tree::ptree ptree;
                input( iss, ptree );
                std::ostringstream oss;
                output( oss, ptree,  indices_mode );
                std::string s = oss.str();
                if( s.empty() ) { continue; }
                bool escaped = false;
                bool quoted = false;
                for( std::size_t i = 0; i < s.size(); ++i ) // quick and dirty
                {
                    if( escaped ) { escaped = false; continue; }
                    switch( s[i] )
                    {
                        case '\r': if( !quoted ) { s[i] = ' '; } break;
                        case '\\': escaped = true; break;
                        case '"' : quoted = !quoted; break;
                        case '\n': if( !quoted ) { s[i] = ' '; } break;
                    }

                }
                std::cout << s << std::endl;
            }
        }
        else
        {
            boost::property_tree::ptree ptree;
            input( std::cin, ptree );
            output( std::cout, ptree, indices_mode );
        }
        return 0;
    }
    catch( boost::property_tree::ptree_bad_data& ex )
    {
        std::cerr << "name-value-convert: bad data: " << ex.what() << std::endl;
    }
    catch( boost::property_tree::ptree_bad_path& ex )
    {
        std::cerr << "name-value-convert: bad path: " << ex.what() << std::endl;
    }
    catch( boost::property_tree::ptree_error& ex )
    {
        boost::regex e( "<unspecified file>" );
        std::cerr << "name-value-convert: parsing error: " << boost::regex_replace( std::string( ex.what() ), e, "line" ) << std::endl;
    }
    catch( std::exception& ex )
    {
        std::cerr << "name-value-convert: " << ex.what() << std::endl;
    }
    catch( ... )
    {
        std::cerr << "name-value-convert: unknown exception" << std::endl;
    }
    return 1;
}
