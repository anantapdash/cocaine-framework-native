#ifndef COCAINE_FRAMEWORK_SERVICE_DETAIL_HPP
#define COCAINE_FRAMEWORK_SERVICE_DETAIL_HPP

#include <cocaine/framework/service_client/error.hpp>
#include <cocaine/framework/generator.hpp>

#include <cocaine/rpc/message.hpp>
#include <cocaine/rpc/protocol.hpp>
#include <cocaine/traits/typelist.hpp>
#include <cocaine/traits/tuple.hpp>
#include <cocaine/messages.hpp>

#include <ev++.h>

#include <memory>
#include <string>

namespace cocaine { namespace framework {

typedef uint64_t
        session_id_t;

class service_connection_t;

namespace detail { namespace service {

    struct service_handler_concept_t {
        virtual
        ~service_handler_concept_t() {
            // pass
        }

        virtual
        void
        handle_message(const cocaine::io::message_t&) = 0;

        virtual
        void
        error(std::exception_ptr e) = 0;
    };

    template<template<class...> class Wrapper, class... Args>
    struct wrapper_traits {
        typedef Wrapper<Args...> type;
    };

    template<template<class...> class Wrapper, class... Args>
    struct wrapper_traits<Wrapper, std::tuple<Args...>> {
        typedef Wrapper<Args...> type;
    };

    template<class Event, class ResultType = typename cocaine::io::event_traits<Event>::result_type>
    struct unpacker {
        typedef typename wrapper_traits<cocaine::framework::stream, ResultType>::type promise_type;
        typedef typename wrapper_traits<cocaine::framework::generator, ResultType>::type future_type;

        static
        inline
        void
        unpack(promise_type& p,
               std::string& data)
        {
            msgpack::unpacked msg;
            msgpack::unpack(&msg, data.data(), data.size());

            ResultType r;
            cocaine::io::type_traits<ResultType>::unpack(msg.get(), r);
            p.write(std::move(r));
        }
    };

    template<class Event>
    struct unpacker<Event, cocaine::io::raw_t> {
        typedef typename wrapper_traits<cocaine::framework::stream, std::string>::type promise_type;
        typedef typename wrapper_traits<cocaine::framework::generator, std::string>::type future_type;

        static
        inline
        void
        unpack(promise_type& p,
               std::string& data)
        {
            p.write(std::move(data));
        }
    };

    template<class Event, class ResultType = typename cocaine::io::event_traits<Event>::result_type>
    struct message_handler {
        static
        inline
        void
        handle(typename unpacker<Event>::promise_type& p,
               const cocaine::io::message_t& message)
        {
            if (message.id() == io::event_traits<io::rpc::chunk>::id) {
                std::string data;
                message.as<cocaine::io::rpc::chunk>(data);
                unpacker<Event>::unpack(p, data);
            } else if (message.id() == io::event_traits<io::rpc::error>::id) {
                int code;
                std::string msg;
                message.as<cocaine::io::rpc::error>(code, msg);
                p.error(cocaine::framework::make_exception_ptr(
                    service_error_t(std::error_code(code, service_response_category()), msg)
                ));
            }
        }
    };

    template<class Event>
    struct message_handler<Event, void> {
        static
        inline
        void
        handle(typename unpacker<Event>::promise_type& p,
               const cocaine::io::message_t& message)
        {
            if (message.id() == io::event_traits<io::rpc::error>::id) {
                int code;
                std::string msg;
                message.as<cocaine::io::rpc::error>(code, msg);
                p.error(cocaine::framework::make_exception_ptr(
                    service_error_t(std::error_code(code, service_response_category()), msg)
                ));
            }
        }
    };

    template<class Event>
    class service_handler :
        public service_handler_concept_t
    {
        COCAINE_DECLARE_NONCOPYABLE(service_handler)

    public:
        typedef typename unpacker<Event>::future_type
                future_type;

        typedef typename unpacker<Event>::promise_type
                promise_type;

        service_handler()
        {
            // pass
        }

        service_handler(service_handler&& other) :
            m_promise(std::move(other.m_promise))
        {
            // pass
        }

        future_type
        get_future() {
            return m_promise.get_generator();
        }

        void
        handle_message(const cocaine::io::message_t& message) {
            message_handler<Event>::handle(m_promise, message);
        }

        void
        error(std::exception_ptr e) {
            m_promise.error(e);
        }

    protected:
        promise_type m_promise;
    };


    class session_data_t {
    public:
        session_data_t();

        session_data_t(const std::shared_ptr<service_connection_t>& connection,
                       session_id_t id,
                       std::shared_ptr<detail::service::service_handler_concept_t>&& handler);

        ~session_data_t();

        void
        set_timeout(float seconds);

        void
        stop_timer();

        detail::service::service_handler_concept_t*
        handler() const {
            return m_handler.get();
        }

    private:
        void
        on_timeout(ev::timer&, int);

    private:
        session_id_t m_id;
        std::shared_ptr<detail::service::service_handler_concept_t> m_handler;
        std::shared_ptr<ev::timer> m_close_timer;
        bool m_stopped;
        std::shared_ptr<service_connection_t> m_connection;
    };

}} // namespace detail::service

}} // namespace cocaine::framework

#endif // COCAINE_FRAMEWORK_SERVICE_DETAIL_HPP