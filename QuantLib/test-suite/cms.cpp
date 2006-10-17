/* -*- mode: c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

/*
 Copyright (C) 2006 Mario Pucci

 This file is part of QuantLib, a free-software/open-source library
 for financial quantitative analysts and developers - http://quantlib.org/

 QuantLib is free software: you can redistribute it and/or modify it
 under the terms of the QuantLib license.  You should have received a
 copy of the license along with this program; if not, please email
 <quantlib-dev@lists.sf.net>. The license is also available online at
 <http://quantlib.org/reference/license.html>.

 This program is distributed in the hope that it will be useful, but WITHOUT
 ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 FOR A PARTICULAR PURPOSE.  See the license for more details.
*/


#include "cms.hpp"
#include "utilities.hpp"
#include <ql/DayCounters/all.hpp>
#include <ql/Indexes/euribor.hpp>
#include <ql/CashFlows/conundrumpricer.hpp>
#include <ql/TermStructures/all.hpp>
#include <ql/Volatilities/swaptionvolmatrix.hpp>
#include <ql/Volatilities/swaptionvolcube.hpp>
#include <ql/Volatilities/swaptionvolcubebysabr.hpp>

using namespace QuantLib;
using namespace boost::unit_test_framework;

QL_BEGIN_TEST_LOCALS(CmsTest)

// global data

Date today_, settlement_;
Calendar calendar_;
BusinessDayConvention fixedConvention_, floatingConvention_;
Frequency fixedFrequency_, floatingFrequency_;
DayCounter fixedDayCount_;
boost::shared_ptr<Xibor> iborIndex_;
Integer settlementDays_;
Handle<YieldTermStructure> termStructure_;
Date referenceDate_;
Date startDate_;
Date paymentDate_;
Date endDate_;
Real gearing_;
Spread spread_;
Rate infiniteCap_, infiniteFloor_;
Integer years_;
boost::shared_ptr<SwapIndex> index_;
std::string familyName_;

Handle<SwaptionVolatilityStructure> swaptionVolatilityMatrix_;
Handle<SwaptionVolatilityStructure> flatSwaptionVolatilityCube_;
Handle<SwaptionVolatilityStructure> flatSwaptionVolatilityCubeBySabr_;
Handle<SwaptionVolatilityStructure> swaptionVolatilityCubeBySabr_;

Real volatility_;

std::vector<GFunctionFactory::ModelOfYieldCurve> modelOfYieldCurves_;
std::vector<Handle<SwaptionVolatilityStructure> > swaptionVolatilityStructures_;

Real rateTolerance_;
Real priceTolerance_;

void teardown() {
    Settings::instance().evaluationDate() = Date();
}

void setup() {

    settlementDays_ = 2;
    fixedConvention_ = Unadjusted;
    floatingConvention_ = ModifiedFollowing;
    fixedFrequency_ = Semiannual;
    floatingFrequency_ = Semiannual;
    fixedDayCount_ = Thirty360();
    gearing_ = 1.0;
    spread_ = 0.0;
    years_ = 30;

    familyName_ = "";

    volatility_ = 0.09;

    infiniteCap_ = 100.0;
    infiniteFloor_ = -100.0;

    rateTolerance_ = 2.0e-4;
    priceTolerance_ = 2.0e-4;

    iborIndex_ = boost::shared_ptr<Xibor>(
        new Euribor(Period(floatingFrequency_), termStructure_));

    calendar_ = iborIndex_->calendar();
    today_ = calendar_.adjust(Date::todaysDate());
    Settings::instance().evaluationDate() = today_;
    settlement_ = calendar_.advance(today_,settlementDays_,Days);
    termStructure_.linkTo(flatRate(settlement_, 0.05, Actual365Fixed()));
    referenceDate_ = termStructure_->referenceDate();
    startDate_ = (referenceDate_+2*3600);
    paymentDate_ = (startDate_+365);
    endDate_ = (paymentDate_);
    index_ = boost::shared_ptr<SwapIndex>(
        new SwapIndex(
        familyName_,
        years_,
        settlementDays_,
        iborIndex_->currency(),
        calendar_,
        fixedFrequency_,
        fixedConvention_,
        iborIndex_->dayCounter(),
        iborIndex_)
        );


    // Volatility
    std::vector<Date> exerciseDates(2);
    exerciseDates[0] = referenceDate_+30;
    exerciseDates[1] = endDate_;

    std::vector<Period> lengths(2);
    lengths[0] = Period(1, Years);
    lengths[1] = Period(30, Years);

    const Matrix volatilities(2, 2, volatility_);

    swaptionVolatilityMatrix_ = Handle<SwaptionVolatilityStructure>(
        boost::shared_ptr<SwaptionVolatilityStructure>(new
            SwaptionVolatilityMatrix(referenceDate_, exerciseDates, lengths,
                                     volatilities,
                                     iborIndex_->dayCounter())));

    std::vector<Rate> strikeSpreads;

    strikeSpreads.push_back(-.02);
    strikeSpreads.push_back(-.01);
    strikeSpreads.push_back(-.005);
    strikeSpreads.push_back(-.0025);
    strikeSpreads.push_back(.0);
    strikeSpreads.push_back(.0025);
    strikeSpreads.push_back(.005);
    strikeSpreads.push_back(.01);
    strikeSpreads.push_back(.02);

    Size rows = lengths.size()*lengths.size();
    Size columns = strikeSpreads.size();
    std::vector<std::vector<Handle<Quote> > > nullVolSpreads(rows);
    for (Size i=0; i<rows; i++) {
        nullVolSpreads[i] = std::vector<Handle<Quote> >(columns);
        for (Size j=0; j<columns; j++) {
            nullVolSpreads[i][j].linkTo(
                boost::shared_ptr<Quote>(new SimpleQuote(0.0)));
        }
    }

    flatSwaptionVolatilityCube_ = Handle<SwaptionVolatilityStructure>(
        boost::shared_ptr<SwaptionVolatilityStructure>(new
        SwaptionVolatilityCubeByLinear(
            swaptionVolatilityMatrix_,
            lengths,
            lengths,
            strikeSpreads,
            nullVolSpreads,
            calendar_,
            2,
            fixedFrequency_,
            fixedConvention_,
            iborIndex_->dayCounter(),
            iborIndex_,
            1,
            iborIndex_
           )));
    Matrix parametersGuess(lengths.size()*lengths.size(),4, 0.0);

    for(Size i=0; i<lengths.size()*lengths.size(); i++) {
        parametersGuess[i][0] = .002*i+0.001;
        parametersGuess[i][1] = 1.;
        parametersGuess[i][2] = .4;
        parametersGuess[i][3] = 0.;
    }

    std::vector<bool> isParameterFixed(4,false);
    isParameterFixed[1]=true;

    const Matrix nullVolSpreads2(lengths.size()*lengths.size(),
        strikeSpreads.size(), 0.0);

    std::vector<std::vector<Handle<Quote> > > nullVolSpreadsQuotes(rows);
    for (Size i=0; i<rows; i++) {
        nullVolSpreadsQuotes[i] = std::vector<Handle<Quote> >(columns);
        for (Size j=0; j<columns; j++) {
            nullVolSpreadsQuotes[i][j].linkTo(
                boost::shared_ptr<Quote>(new SimpleQuote(nullVolSpreads2[i][j])));
        }
    }

    boost::shared_ptr<SwaptionVolatilityCubeBySabr> flatSwaptionVolatilityCubeBySabr = 
        boost::shared_ptr<SwaptionVolatilityCubeBySabr> (new
        SwaptionVolatilityCubeBySabr(
            swaptionVolatilityMatrix_,
            lengths,
            lengths,
            strikeSpreads,
            nullVolSpreadsQuotes,
            calendar_,
            2,
            fixedFrequency_,
            fixedConvention_,
            iborIndex_->dayCounter(),
            iborIndex_,
            1,
            iborIndex_,
            parametersGuess,
            isParameterFixed,
            false
            ));
    flatSwaptionVolatilityCubeBySabr->performCalibration();
    flatSwaptionVolatilityCubeBySabr_ = Handle<SwaptionVolatilityStructure>(
        boost::shared_ptr<SwaptionVolatilityStructure>(flatSwaptionVolatilityCubeBySabr));
    

    Matrix volSpreads(lengths.size()*lengths.size(),
        strikeSpreads.size(), 0.0);

    for(Size i=0; i<strikeSpreads.size(); i++) {
        const double x = strikeSpreads[i];
        const double vs = 10*x*x;
        for(Size j=0; j<lengths.size()*lengths.size(); j++) {
            volSpreads[j][i] = vs;
        }
    }

    std::vector<std::vector<Handle<Quote> > > volSpreadsQuotes(rows);
    for (Size i=0; i<rows; i++) {
        volSpreadsQuotes[i] = std::vector<Handle<Quote> >(columns);
        for (Size j=0; j<columns; j++) {
            volSpreadsQuotes[i][j].linkTo(
                boost::shared_ptr<Quote>(new SimpleQuote(volSpreads[i][j])));
        }
    }
    boost::shared_ptr<SwaptionVolatilityCubeBySabr> swaptionVolatilityCubeBySabr = 
        boost::shared_ptr<SwaptionVolatilityCubeBySabr> (new 
        SwaptionVolatilityCubeBySabr(
            swaptionVolatilityMatrix_,
            lengths,
            lengths,
            strikeSpreads,
            volSpreadsQuotes,
            calendar_,
            2,
            fixedFrequency_,
            fixedConvention_,
            iborIndex_->dayCounter(),
            iborIndex_,
            1,
            iborIndex_,
            parametersGuess,
            isParameterFixed,
            false
            ));
    swaptionVolatilityCubeBySabr->performCalibration();
    swaptionVolatilityCubeBySabr_ = Handle<SwaptionVolatilityStructure>(
        boost::shared_ptr<SwaptionVolatilityStructure>(swaptionVolatilityCubeBySabr));



    swaptionVolatilityStructures_.push_back(swaptionVolatilityMatrix_);
    //swaptionVolatilityStructures_.push_back(flatSwaptionVolatilityCube_);
    swaptionVolatilityStructures_.push_back(flatSwaptionVolatilityCubeBySabr_);
    //swaptionVolatilityStructures_.push_back(swaptionVolatilityCubeBySabr_);

    {
        modelOfYieldCurves_.push_back(GFunctionFactory::standard);
        modelOfYieldCurves_.push_back(GFunctionFactory::exactYield);
        modelOfYieldCurves_.push_back(GFunctionFactory::parallelShifts);
        modelOfYieldCurves_.push_back(GFunctionFactory::nonParallelShifts);
    }
}


QL_END_TEST_LOCALS(CmsTest)

void CmsTest::testFairRate()  {

    BOOST_MESSAGE(
            "Testing fair-rate calculation for constant-maturity coupons...");

    QL_TEST_BEGIN
    QL_TEST_SETUP

    for(Size h=0; h<modelOfYieldCurves_.size(); h++) {

    boost::shared_ptr<VanillaCMSCouponPricer> numericalPricer(new
        ConundrumPricerByNumericalIntegration(modelOfYieldCurves_[h], 0, 1));
    boost::shared_ptr<VanillaCMSCouponPricer> analyticPricer(new
        ConundrumPricerByBlack(modelOfYieldCurves_[h]));

    //Coupons
    CMSCoupon coupon1(1,
        paymentDate_, index_, startDate_, endDate_, settlementDays_,
        iborIndex_->dayCounter(),
        numericalPricer, gearing_, spread_, infiniteCap_, infiniteFloor_);
    coupon1.setSwaptionVolatility(swaptionVolatilityMatrix_);

    CMSCoupon coupon2(1,
        paymentDate_, index_, startDate_, endDate_, settlementDays_,
        iborIndex_->dayCounter(),
        analyticPricer, gearing_, spread_, infiniteCap_, infiniteFloor_);
    coupon2.setSwaptionVolatility(swaptionVolatilityMatrix_);

    //Computation
    const double rate1 = coupon1.rate();
    const double rate2 = coupon2.rate();
    const double difference =  rate2-rate1;

    if (std::fabs(difference) > rateTolerance_) {
            BOOST_ERROR("\n" <<
                "startDate:\t" << startDate_ << "\n"
                        "rate1:\t" << io::rate(rate1) << "\n"
                        "rate2:\t" << io::rate(rate2) << "\n"
                        "difference:\t" << io::rate(difference) << "\n"
                        "tolerance: \t" << io::rate(rateTolerance_));
    }
    }

    QL_TEST_TEARDOWN
}

void CmsTest::testParity() {

    BOOST_MESSAGE("Testing put-call parity for constant-maturity coupons...");

    QL_TEST_BEGIN
    QL_TEST_SETUP

    int priceIndex = 1;

    for(Size modelOfYieldCurveIndex=0;
        modelOfYieldCurveIndex<modelOfYieldCurves_.size();
        modelOfYieldCurveIndex++) {

        std::vector<boost::shared_ptr<VanillaCMSCouponPricer> > pricers;
        {
            boost::shared_ptr<VanillaCMSCouponPricer> analyticPricer(
                new ConundrumPricerByBlack(modelOfYieldCurves_[
                    modelOfYieldCurveIndex]));
            pricers.push_back(analyticPricer);

            boost::shared_ptr<VanillaCMSCouponPricer> numericalPricer(
                new ConundrumPricerByNumericalIntegration(
                modelOfYieldCurves_[modelOfYieldCurveIndex],
                0, 1));
            pricers.push_back(numericalPricer);
        }


        for(Size volStructureIndex = 0;
            volStructureIndex < swaptionVolatilityStructures_.size();
        volStructureIndex++) {

            for(Size pricerIndex=0; pricerIndex<pricers.size(); pricerIndex++) {

                CMSCoupon swaplet(1,
                    paymentDate_, index_, startDate_, endDate_, settlementDays_,
                    iborIndex_->dayCounter(),
                    pricers[pricerIndex], gearing_, spread_, infiniteCap_, infiniteFloor_);
                swaplet.setSwaptionVolatility(
                    swaptionVolatilityStructures_[volStructureIndex]
                );

                Real strike = .02;

                for(Size strikeIndex=0; strikeIndex<10; strikeIndex++) {

                //BOOST_MESSAGE("("
                //    << volStructureIndex << ", "
                //    << pricerIndex << ", "
                //    << modelOfYieldCurveIndex << ", "
                //    << strikeIndex
                //    << ")\t" << "testing " << priceIndex
                //    << "-th cms swaplet, caplet and floolet with strike "
                //    << io::rate(strike) << "\n");

                    strike += .005;
                    CMSCoupon caplet(1,
                        paymentDate_, index_, startDate_, endDate_, settlementDays_,
                        iborIndex_->dayCounter(),
                        pricers[pricerIndex], gearing_, spread_, strike, infiniteFloor_);
                    caplet.setSwaptionVolatility(
                        swaptionVolatilityStructures_[volStructureIndex]);

                    CMSCoupon floorlet(1,
                        paymentDate_, index_, startDate_, endDate_, settlementDays_,
                        iborIndex_->dayCounter(),
                        pricers[pricerIndex], gearing_, spread_, infiniteCap_, strike);
                    floorlet.setSwaptionVolatility(
                        swaptionVolatilityStructures_[volStructureIndex]);

                    //Computation
                    const double price1 = swaplet.price(termStructure_)
                        + swaplet.accrualPeriod()*strike* termStructure_->discount(paymentDate_);
                    const double price2 = floorlet.price(termStructure_)
                        + caplet.price(termStructure_);

                    priceIndex++;

                    const double difference =  price1-price2;

                    if (std::fabs(difference) > priceTolerance_) {
                            BOOST_ERROR("\n" <<
                                "startDate:\t" << startDate_ << "\n"
                                "strike:\t" << strike << "\n"
                                "price1:\t" << io::rate(price1) << "\n"
                                "price2:\t" << io::rate(price2) << "\n"
                                "difference:\t" << io::rate(difference) << "\n"
                                "tolerance: \t" << io::rate(priceTolerance_));
                    }
                }
            }
        }
    }

    QL_TEST_TEARDOWN
}


void CmsTest::testCmsSwap() {

    BOOST_MESSAGE("Testing constant-maturity swaps...");

    QL_TEST_BEGIN
    QL_TEST_SETUP

     {
        modelOfYieldCurves_.clear();
    //  modelOfYieldCurves_.push_back(GFunctionFactory::standard);
    //  modelOfYieldCurves_.push_back(GFunctionFactory::exactYield);
        modelOfYieldCurves_.push_back(GFunctionFactory::parallelShifts);
        modelOfYieldCurves_.push_back(GFunctionFactory::nonParallelShifts);
    }

    std::vector<Size> swapLengths;
    swapLengths.push_back(1);
    swapLengths.push_back(5);
    swapLengths.push_back(6);
    swapLengths.push_back(10);
    swapLengths.push_back(15);
    swapLengths.push_back(20);
    //swapLengths.push_back(30);

    int priceIndex = 1;

    for(Size swapLengthIndex = 0; swapLengthIndex<swapLengths.size();
        swapLengthIndex++) {

    const int n = swapLengths[swapLengthIndex];

    std::vector<Real> meanReversions(n, 0);
    std::vector<Real> fixedNominals(n, 1);
    std::vector<Real> floatingNominals(n, 1);
    std::vector<Real> caps(n, infiniteCap_);
    std::vector<Real> floors(n, infiniteFloor_);
    std::vector<Real> fractions(n, gearing_);
    std::vector<Real> baseRate(n, 0);



    for(Size volStructureIndex = 0;
        volStructureIndex < swaptionVolatilityStructures_.size();
        volStructureIndex++) {

        for(Size modelOfYieldCurveIndex=0;
            modelOfYieldCurveIndex<modelOfYieldCurves_.size();
            modelOfYieldCurveIndex++) {

            std::vector<boost::shared_ptr<VanillaCMSCouponPricer> > pricers;
            {
                boost::shared_ptr<VanillaCMSCouponPricer> analyticPricer(
                    new ConundrumPricerByBlack(
                    modelOfYieldCurves_[modelOfYieldCurveIndex]));
                pricers.push_back(analyticPricer);

                boost::shared_ptr<VanillaCMSCouponPricer> numericalPricer(
                    new ConundrumPricerByNumericalIntegration(
                    modelOfYieldCurves_[modelOfYieldCurveIndex],
                    0, 1));
                pricers.push_back(numericalPricer);
            }

            Date startDate = today_;
            startDate += Period(10, Days);
            Date maturityDate = startDate;
            maturityDate += Period(n, Years);

            Schedule fixedSchedule(startDate,maturityDate,Period(fixedFrequency_),
                                   calendar_,fixedConvention_,fixedConvention_,
                                   false,false);
            Schedule floatingSchedule(startDate,maturityDate,Period(floatingFrequency_),
                                      calendar_,floatingConvention_,fixedConvention_,
                                      false,false);

            //Schedule fixedSchedule(calendar_,startDate, maturityDate,
            //    fixedFrequency_,fixedConvention_);
            //Schedule floatingSchedule(calendar_,startDate, maturityDate,
            //    floatingFrequency_,floatingConvention_);

            std::vector<Real> prices;
            //BOOST_MESSAGE("\n$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$\n\n");
            for(Size pricerIndex=0; pricerIndex<pricers.size();
                pricerIndex++) {

                std::vector<boost::shared_ptr<CashFlow> > cmsLeg =
                    CMSCouponVector(
                    fixedSchedule,
                    fixedConvention_,
                    fixedNominals,
                    index_,
                    settlementDays_,
                    fixedDayCount_,
                    baseRate,
                    fractions,
                    caps,
                    floors,
                    meanReversions,
                    pricers[pricerIndex],
                    swaptionVolatilityStructures_[volStructureIndex]
                );

                std::vector<boost::shared_ptr<CashFlow> > floatingLeg =
                    FloatingRateCouponVector(
                    floatingSchedule,
                    floatingConvention_,
                    floatingNominals,
                    settlementDays_,
                    iborIndex_,
                    std::vector<Real>(),
                    std::vector<Spread>(),
                    iborIndex_->dayCounter());


                boost::shared_ptr<Swap> swap =  boost::shared_ptr<Swap>(
                    new Swap(termStructure_, cmsLeg, floatingLeg));


                //BOOST_MESSAGE("("
                //    << volStructureIndex << ", "
                //    << pricerIndex << ", "
                //    << modelOfYieldCurveIndex << ", "
                //    << swapLengthIndex << ")\t"
                //    << priceIndex << "-th pricing "
                //    << "for cms swap test...\n");

                if(true || priceIndex == 23 || priceIndex == 24) {
                    const Real price = swap->NPV();
                    prices.push_back(price);
                }
                else {
                    prices.push_back(0);
                }
                priceIndex++;
            }
            const double difference =  prices[0]-prices[1];
            //BOOST_MESSAGE("\n" << "startDate:\t" << startDate << "\n"
            //        "maturityDate:\t" << maturityDate << "\n"
            //        "swapLength:\t" << swapLengths[swapLengthIndex] << "\n"
            //        "price analytic:\t" << io::rate(prices[0]) << "\n"
            //        "price numerical:\t" << io::rate(prices[1]) << "\n");

            if (std::fabs(difference) > priceTolerance_) {
                BOOST_ERROR("\n" <<
                            "difference:\t" << io::rate(difference) << "\n"
                            "tolerance: \t" << io::rate(priceTolerance_));
            }
            else {
                //BOOST_MESSAGE("difference = " << io::rate(difference) << "\n");
            }
        }
    }
    }

    QL_TEST_TEARDOWN
}

test_suite* CmsTest::suite() {
    test_suite* suite = BOOST_TEST_SUITE("CMS tests");
    suite->add(BOOST_TEST_CASE(&CmsTest::testFairRate));
    suite->add(BOOST_TEST_CASE(&CmsTest::testParity));
    suite->add(BOOST_TEST_CASE(&CmsTest::testCmsSwap));
    return suite;
}

